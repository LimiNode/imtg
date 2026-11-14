#include "imtg/storage/mdbx_storage.hpp"

#include <mdbx_containers/common.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace imtg {

namespace {

constexpr std::array<const char*, 6> physical_names = {
    "meta", "sessions", "processed_updates", "render_intents", "outbox",
    "event_journal"};
constexpr std::string_view schema_key = "imtg/schema/v1";
constexpr char record_magic[] = "IMR1";
constexpr char schema_magic[] = "IMS1";
constexpr std::size_t max_scan_page = 4096;
constexpr unsigned char physical_key_format_version = 1;
constexpr unsigned char internal_schema_namespace = 0;

enum class PhysicalNamespace : std::size_t {
    Meta = 0,
    Sessions,
    ProcessedUpdates,
    RenderIntents,
    Outbox,
    EventJournal,
};

struct NamespaceRouteDefinition {
    std::string_view logical_name;
    PhysicalNamespace physical_namespace;
    unsigned char logical_prefix;
};

constexpr std::array<NamespaceRouteDefinition, 11> namespace_routes = {{
    {"meta", PhysicalNamespace::Meta, 1},
    {"sessions", PhysicalNamespace::Sessions, 1},
    {"processed_updates", PhysicalNamespace::ProcessedUpdates, 1},
    {"render_intents", PhysicalNamespace::RenderIntents, 1},
    {"render_intent_claims", PhysicalNamespace::RenderIntents, 2},
    {"render_intent_generations", PhysicalNamespace::RenderIntents, 3},
    {"outbox_effects", PhysicalNamespace::Outbox, 1},
    {"outbox_dispatch_targets", PhysicalNamespace::Outbox, 2},
    {"applied_targets", PhysicalNamespace::Outbox, 3},
    {"outbox", PhysicalNamespace::Outbox, 4},
    {"event_journal", PhysicalNamespace::EventJournal, 1},
}};

struct ResolvedNamespace {
    MDBX_dbi dbi;
    unsigned char logical_prefix;
};

struct PhysicalState {
    bool exists = false;
    std::optional<StorageBytes> value;
    StorageRevision revision{};
};

struct RecordKey {
    std::string name_space;
    std::string key;

    friend bool operator<(const RecordKey& left, const RecordKey& right) noexcept
    {
        return left.name_space < right.name_space
            || (left.name_space == right.name_space && left.key < right.key);
    }
};

struct PendingRecord {
    std::optional<StorageBytes> value;
    StorageRevision expected_revision{};
    StorageRevision resulting_revision{};
};

Error storage_error(ErrorCode code, std::string message)
{
    return Error{code, std::move(message)};
}

Error map_mdbx_error(int rc, const char* context)
{
    ErrorCode code = ErrorCode::StorageUnavailable;
    if (rc == MDBX_EACCESS || rc == MDBX_EPERM) {
        code = ErrorCode::StorageReadOnly;
    } else if (rc == MDBX_BAD_VALSIZE) {
        code = ErrorCode::StorageRecordTooLarge;
    } else if (rc == MDBX_NOTFOUND) {
        code = ErrorCode::StorageNotFound;
    } else if (rc == MDBX_KEYEXIST) {
        code = ErrorCode::StorageConflict;
    } else if (rc == MDBX_WANNA_RECOVERY) {
        code = ErrorCode::StorageSchemaFailure;
    }
    return storage_error(code, std::string(context) + ": (" + std::to_string(rc)
                             + ") " + mdbx_strerror(rc));
}

Error map_exception(const std::exception& exception)
{
    const auto* mdbx_exception = dynamic_cast<const mdbxc::MdbxException*>(&exception);
    if (mdbx_exception != nullptr && mdbx_exception->error_code() != -1) {
        return map_mdbx_error(mdbx_exception->error_code(), exception.what());
    }
    return storage_error(ErrorCode::StorageUnavailable, exception.what());
}

std::string encode_physical_key(unsigned char logical_prefix, std::string_view key)
{
    std::string encoded;
    encoded.reserve(2 + key.size());
    encoded.push_back(static_cast<char>(physical_key_format_version));
    encoded.push_back(static_cast<char>(logical_prefix));
    encoded.append(key);
    return encoded;
}

void append_u32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>((value >> 24u) & 0xffu));
    output.push_back(static_cast<char>((value >> 16u) & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
    output.push_back(static_cast<char>(value & 0xffu));
}

void append_u64(std::string& output, std::uint64_t value)
{
    for (unsigned shift = 56; shift != std::numeric_limits<unsigned>::max(); shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffu));
        if (shift == 0) break;
    }
}

Result<std::uint32_t> read_u32(std::string_view input, std::size_t& offset)
{
    if (input.size() - offset < 4) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "truncated MDBX u32");
    }
    const auto byte = [&](std::size_t index) {
        return static_cast<std::uint32_t>(
            static_cast<unsigned char>(input[offset + index]));
    };
    const std::uint32_t value = (byte(0) << 24u) | (byte(1) << 16u)
        | (byte(2) << 8u) | byte(3);
    offset += 4;
    return value;
}

Result<std::uint64_t> read_u64(std::string_view input, std::size_t& offset)
{
    if (input.size() - offset < 8) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "truncated MDBX u64");
    }
    std::uint64_t value = 0;
    for (unsigned index = 0; index != 8; ++index) {
        value = (value << 8u)
            | static_cast<std::uint64_t>(
                static_cast<unsigned char>(input[offset + index]));
    }
    offset += 8;
    return value;
}

Result<std::string> encode_record(const std::optional<StorageBytes>& value,
                                  StorageRevision revision)
{
    if (revision == 0) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "MDBX record revision must be non-zero");
    }
    const std::size_t header_size = 4 + 1 + 8;
    if (value.has_value()
        && value->size() > static_cast<std::size_t>(MDBX_MAXDATASIZE) - header_size) {
        return storage_error(ErrorCode::StorageRecordTooLarge,
                             "MDBX record value exceeds the native limit");
    }
    std::string encoded;
    encoded.reserve(header_size + (value ? value->size() : 0));
    encoded.append(record_magic, 4);
    encoded.push_back(value.has_value() ? '\x01' : '\x00');
    append_u64(encoded, revision);
    if (value.has_value()) encoded.append(*value);
    return encoded;
}

Result<PhysicalState> decode_record(std::string_view encoded)
{
    if (encoded.size() < 13 || encoded.compare(0, 4, record_magic, 4) != 0) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "MDBX record has an unknown format");
    }
    const unsigned char flags = static_cast<unsigned char>(encoded[4]);
    if (flags > 1) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "MDBX record has invalid flags");
    }
    std::size_t offset = 5;
    const auto revision = read_u64(encoded, offset);
    if (!revision) return revision.error();
    if (revision.value() == 0) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "MDBX record has a zero revision");
    }
    PhysicalState state;
    state.exists = true;
    state.revision = revision.value();
    if (flags == 1) state.value = StorageBytes(encoded.substr(offset));
    return state;
}

Result<std::string> encode_schema(const StorageSchema& schema)
{
    if (schema.namespace_versions.size() > std::numeric_limits<std::uint32_t>::max()) {
        return storage_error(ErrorCode::StorageSerializationFailure,
                             "too many MDBX schema namespaces");
    }
    std::vector<std::pair<std::string, std::uint32_t>> namespaces(
        schema.namespace_versions.begin(), schema.namespace_versions.end());
    std::sort(namespaces.begin(), namespaces.end(),
              [](const auto& left, const auto& right) {
                  return left.first < right.first;
              });
    std::string encoded(schema_magic, 4);
    append_u32(encoded, schema.profile_version);
    encoded.push_back(static_cast<char>(schema.migration_state));
    append_u32(encoded, static_cast<std::uint32_t>(namespaces.size()));
    for (const auto& entry : namespaces) {
        if (entry.first.size() > std::numeric_limits<std::uint32_t>::max()) {
            return storage_error(ErrorCode::StorageSerializationFailure,
                                 "MDBX schema namespace name is too long");
        }
        append_u32(encoded, static_cast<std::uint32_t>(entry.first.size()));
        encoded.append(entry.first);
        append_u32(encoded, entry.second);
    }
    return encoded;
}

Result<StorageSchema> decode_schema(std::string_view encoded)
{
    if (encoded.size() < 13 || encoded.compare(0, 4, schema_magic, 4) != 0) {
        return storage_error(ErrorCode::StorageSchemaFailure,
                             "MDBX schema metadata has an unknown format");
    }
    std::size_t offset = 4;
    const auto profile = read_u32(encoded, offset);
    if (!profile) return profile.error();
    if (offset >= encoded.size()) {
        return storage_error(ErrorCode::StorageSchemaFailure,
                             "MDBX schema metadata has no migration state");
    }
    const auto migration = static_cast<unsigned char>(encoded[offset++]);
    if (migration > static_cast<unsigned char>(MigrationState::RecoveryRequired)) {
        return storage_error(ErrorCode::StorageSchemaFailure,
                             "MDBX schema metadata has an invalid migration state");
    }
    const auto count = read_u32(encoded, offset);
    if (!count) return count.error();
    StorageSchema schema;
    schema.profile_version = profile.value();
    schema.migration_state = static_cast<MigrationState>(migration);
    for (std::uint32_t index = 0; index != count.value(); ++index) {
        const auto length = read_u32(encoded, offset);
        if (!length || encoded.size() - offset < length.value()) {
            return storage_error(ErrorCode::StorageSchemaFailure,
                                 "truncated MDBX schema namespace");
        }
        std::string name(encoded.substr(offset, length.value()));
        offset += length.value();
        const auto version = read_u32(encoded, offset);
        if (!version || !schema.namespace_versions.emplace(std::move(name), version.value()).second) {
            return storage_error(ErrorCode::StorageSchemaFailure,
                                 "duplicate MDBX schema namespace");
        }
    }
    if (offset != encoded.size()) {
        return storage_error(ErrorCode::StorageSchemaFailure,
                             "trailing bytes in MDBX schema metadata");
    }
    return schema;
}

} // namespace

class MdbxStorageBackend::Impl {
public:
    class Transaction;

    explicit Impl(MdbxStorageConfig config_value)
        : config(std::move(config_value))
    {
    }

    ~Impl()
    {
        if (connection && connection->is_connected()) {
            try {
                connection->disconnect();
            } catch (...) {
            }
        }
    }

    Result<void> initialize()
    {
        if (config.path.empty() || config.max_dbs < static_cast<std::int64_t>(physical_names.size())) {
            return storage_error(ErrorCode::InvalidArgument,
                                 "MDBX path is empty or max_dbs is below the fixed profile");
        }
        try {
            mdbxc::Config native_config;
            native_config.pathname = config.path;
            native_config.max_dbs = config.max_dbs;
            native_config.max_readers = config.max_readers;
            native_config.read_only = config.read_only;
            native_config.no_subdir = config.no_subdir;
            native_config.sync_durable = config.sync_durable;
            // Config's zero page size selects MDBX's minimum (256), not its OS default.
            native_config.page_size = 16384;
            connection = mdbxc::Connection::create(native_config);
            const int native_key_limit = mdbx_env_get_maxkeysize_ex(
                connection->env_handle(), MDBX_DB_DEFAULTS);
            if (native_key_limit <= 2) {
                return storage_error(ErrorCode::StorageUnavailable, "invalid MDBX key-size limit");
            }
            max_logical_key_size = static_cast<std::size_t>(native_key_limit) - 2;
            auto native = connection->transaction(
                config.read_only ? mdbxc::TransactionMode::READ_ONLY
                                 : mdbxc::TransactionMode::WRITABLE);
            const auto flags = config.read_only
                ? static_cast<MDBX_db_flags_t>(MDBX_DB_DEFAULTS)
                : static_cast<MDBX_db_flags_t>(MDBX_DB_DEFAULTS | MDBX_CREATE);
            for (std::size_t index = 0; index != physical_names.size(); ++index) {
                int rc = mdbx_dbi_open(native.handle(), physical_names[index], flags,
                                       &dbis[index]);
                if (rc != MDBX_SUCCESS) return map_mdbx_error(rc, "opening MDBX DBI");
            }

            const auto encoded_schema_key = encode_physical_key(internal_schema_namespace, schema_key);
            MDBX_val key{const_cast<char*>(encoded_schema_key.data()), encoded_schema_key.size()};
            MDBX_val value{};
            const int get_rc = mdbx_get(native.handle(), dbis[0], &key, &value);
            if (get_rc == MDBX_NOTFOUND) {
                MDBX_val legacy_key{const_cast<char*>(schema_key.data()), schema_key.size()};
                const int legacy_rc = mdbx_get(native.handle(), dbis[0], &legacy_key, &value);
                if (legacy_rc == MDBX_SUCCESS) {
                    return storage_error(ErrorCode::StorageSchemaFailure,
                                         "unprefixed draft MDBX format requires explicit migration");
                }
                if (legacy_rc != MDBX_NOTFOUND) return map_mdbx_error(legacy_rc, "checking legacy MDBX schema");
                if (config.read_only) {
                    return storage_error(ErrorCode::StorageSchemaFailure,
                                         "read-only MDBX environment has no schema metadata");
                }
                const auto encoded = encode_schema(config.schema);
                if (!encoded) return encoded.error();
                MDBX_val encoded_value{const_cast<char*>(encoded.value().data()),
                                       encoded.value().size()};
                const int put_rc = mdbx_put(native.handle(), dbis[0], &key,
                                            &encoded_value, MDBX_UPSERT);
                if (put_rc != MDBX_SUCCESS) return map_mdbx_error(put_rc, "writing MDBX schema");
                stored_schema = config.schema;
            } else if (get_rc != MDBX_SUCCESS) {
                return map_mdbx_error(get_rc, "reading MDBX schema");
            } else {
                const auto decoded = decode_schema(std::string_view(
                    static_cast<const char*>(value.iov_base), value.iov_len));
                if (!decoded) return decoded.error();
                stored_schema = decoded.value();
            }
            native.commit();
            open = true;
            return Result<void>();
        } catch (const std::exception& exception) {
            return map_exception(exception);
        } catch (...) {
            return storage_error(ErrorCode::StorageUnavailable,
                                 "MDBX environment initialization failed");
        }
    }

    Result<void> close()
    {
        if (!connection) {
            open = false;
            return Result<void>();
        }
        try {
            connection->disconnect();
            connection.reset();
            open = false;
            return Result<void>();
        } catch (const std::exception& exception) {
            return map_exception(exception);
        } catch (...) {
            return storage_error(ErrorCode::StorageUnavailable,
                                 "MDBX environment close failed");
        }
    }

    MdbxStorageConfig config;
    std::shared_ptr<mdbxc::Connection> connection;
    std::array<MDBX_dbi, physical_names.size()> dbis{};
    StorageSchema stored_schema;
    std::size_t max_logical_key_size{};
    bool open = false;
};

class MdbxStorageBackend::Impl::Transaction final : public ITransaction {
public:
    Transaction(Impl& owner, TransactionMode mode)
        : m_owner(owner), m_mode(mode), m_thread(std::this_thread::get_id())
    {
        const auto native_mode = mode == TransactionMode::ReadOnly
            ? mdbxc::TransactionMode::READ_ONLY : mdbxc::TransactionMode::WRITABLE;
        m_native = std::make_unique<mdbxc::Transaction>(
            m_owner.connection->transaction(native_mode));
    }

    ~Transaction() override
    {
        if (m_native && std::this_thread::get_id() != m_thread) std::terminate();
        rollback();
    }

    TransactionMode mode() const noexcept override { return m_mode; }
    TransactionState state() const noexcept override { return m_state; }

    Result<std::optional<StorageRecord>> read(std::string_view name_space,
                                              std::string_view key) override
    {
        const auto precondition = check_active();
        if (!precondition) return precondition.error();
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        const auto state = read_state(dbi.value(), name_space, key);
        if (!state) return state.error();
        if (!state.value().value.has_value()) return std::optional<StorageRecord>{};
        return std::optional<StorageRecord>{StorageRecord{
            *state.value().value, state.value().revision}};
    }

    Result<std::vector<StorageScanEntry>> scan(std::string_view name_space,
                                               std::string_view continuation,
                                               std::size_t limit) override
    {
        const auto precondition = check_active();
        if (!precondition) return precondition.error();
        if (name_space.empty() || limit == 0 || limit > max_scan_page) {
            return storage_error(ErrorCode::InvalidArgument,
                                 "scan namespace and limit are outside the bounded range");
        }
        if (continuation.size() > m_owner.max_logical_key_size) {
            return storage_error(ErrorCode::StorageRecordTooLarge, "MDBX scan cursor exceeds the native key limit");
        }
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        MDBX_cursor* raw_cursor = nullptr;
        int rc = mdbx_cursor_open(m_native->handle(), dbi.value().dbi, &raw_cursor);
        if (rc != MDBX_SUCCESS) return map_mdbx_error(rc, "opening MDBX scan cursor");
        const std::unique_ptr<MDBX_cursor, decltype(&mdbx_cursor_close)> cursor(
            raw_cursor, &mdbx_cursor_close);

        std::vector<StorageScanEntry> entries;
        entries.reserve(limit);
        const auto prefix = encode_physical_key(dbi.value().logical_prefix, {});
        const auto seek = encode_physical_key(dbi.value().logical_prefix, continuation);
        MDBX_val key{const_cast<char*>(seek.data()), seek.size()};
        MDBX_val value{};
        rc = mdbx_cursor_get(cursor.get(), &key, &value, MDBX_SET_RANGE);
        if (!continuation.empty() && rc == MDBX_SUCCESS && key.iov_len == seek.size()
            && std::equal(static_cast<const char*>(key.iov_base),
                          static_cast<const char*>(key.iov_base) + key.iov_len, seek.data())) {
            rc = mdbx_cursor_get(cursor.get(), &key, &value, MDBX_NEXT);
        }
        while (rc == MDBX_SUCCESS && entries.size() < limit) {
            // shortcut: tombstones count toward cursor work, not the result limit;
            // add a physical-progress cursor contract in M2d-b before promising bounded work.
            const std::string_view physical_key(static_cast<const char*>(key.iov_base), key.iov_len);
            if (physical_key.size() < prefix.size()
                || physical_key.substr(0, prefix.size()) != prefix) break;
            const std::string copied_key(physical_key.substr(prefix.size()));
            const auto decoded = decode_record(std::string_view(
                static_cast<const char*>(value.iov_base), value.iov_len));
            if (!decoded) {
                return decoded.error();
            }
            if (decoded.value().value.has_value()) {
                entries.push_back(StorageScanEntry{copied_key,
                    StorageRecord{*decoded.value().value, decoded.value().revision}});
            }
            rc = mdbx_cursor_get(cursor.get(), &key, &value, MDBX_NEXT);
        }
        if (rc != MDBX_SUCCESS && rc != MDBX_NOTFOUND) {
            return map_mdbx_error(rc, "scanning MDBX records");
        }
        return entries;
    }

    Result<void> watch(std::string_view name_space, std::string_view key,
                       StorageRevision expected_revision) override
    {
        const auto precondition = check_active();
        if (!precondition) return precondition.error();
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        const auto state = read_state(dbi.value(), name_space, key);
        if (!state) return state.error();
        if (state.value().revision != expected_revision) {
            return storage_error(ErrorCode::StorageConflict,
                                 "read dependency is stale");
        }
        const RecordKey identity{std::string(name_space), std::string(key)};
        const auto pending = m_writes.find(identity);
        m_dependencies[identity] = pending == m_writes.end()
            ? expected_revision : pending->second.expected_revision;
        return Result<void>();
    }

    Result<StorageRevision> create_if_absent(std::string_view name_space,
                                              std::string_view key,
                                              std::string_view value) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        const RecordKey identity{std::string(name_space), std::string(key)};
        const auto state = read_state(dbi.value(), name_space, key);
        if (!state) return state.error();
        if (state.value().value.has_value()) {
            return storage_error(ErrorCode::StorageConflict, "record already exists");
        }
        const auto pending = m_writes.find(identity);
        const auto revision = pending == m_writes.end()
            ? next_storage_revision(state.value().revision)
            : Result<StorageRevision>(pending->second.resulting_revision);
        if (!revision) return revision.error();
        if (pending == m_writes.end()) {
            m_writes.emplace(identity, PendingRecord{
                StorageBytes(value), state.value().revision, revision.value()});
        } else {
            pending->second.value = StorageBytes(value);
        }
        const auto written = put_record(dbi.value(), key,
                                        m_writes.find(identity)->second.value,
                                        revision.value());
        if (!written) return written.error();
        return revision.value();
    }

    Result<StorageRevision> write(std::string_view name_space,
                                  std::string_view key,
                                  std::string_view value,
                                  StorageRevision expected_revision) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        const RecordKey identity{std::string(name_space), std::string(key)};
        const auto state = read_state(dbi.value(), name_space, key);
        if (!state) return state.error();
        if (!state.value().value.has_value()) {
            return storage_error(ErrorCode::StorageNotFound, "record does not exist");
        }
        if (state.value().revision != expected_revision || expected_revision == 0) {
            return storage_error(ErrorCode::StorageConflict, "record revision is stale");
        }
        const auto pending = m_writes.find(identity);
        const auto revision = pending == m_writes.end()
            ? next_storage_revision(state.value().revision)
            : Result<StorageRevision>(pending->second.resulting_revision);
        if (!revision) return revision.error();
        if (pending == m_writes.end()) {
            m_writes.emplace(identity, PendingRecord{
                StorageBytes(value), state.value().revision, revision.value()});
        } else {
            pending->second.value = StorageBytes(value);
        }
        const auto written = put_record(dbi.value(), key,
                                        m_writes.find(identity)->second.value,
                                        revision.value());
        if (!written) return written.error();
        return revision.value();
    }

    Result<void> erase(std::string_view name_space, std::string_view key,
                       StorageRevision expected_revision) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto dbi = resolve_dbi(name_space);
        if (!dbi) return dbi.error();
        const RecordKey identity{std::string(name_space), std::string(key)};
        const auto state = read_state(dbi.value(), name_space, key);
        if (!state) return state.error();
        if (!state.value().value.has_value()) {
            return storage_error(ErrorCode::StorageNotFound, "record does not exist");
        }
        if (state.value().revision != expected_revision || expected_revision == 0) {
            return storage_error(ErrorCode::StorageConflict, "record revision is stale");
        }
        const auto pending = m_writes.find(identity);
        if (pending != m_writes.end() && pending->second.expected_revision == 0) {
            m_writes.erase(pending);
            return erase_physical(dbi.value(), key);
        }
        if (pending != m_writes.end()) {
            pending->second.value.reset();
            const auto written = put_record(dbi.value(), key, std::nullopt,
                                            pending->second.resulting_revision);
            if (!written) return written.error();
            return Result<void>();
        }
        const auto revision = next_storage_revision(state.value().revision);
        if (!revision) return revision.error();
        m_writes.emplace(identity, PendingRecord{
            std::nullopt, state.value().revision, revision.value()});
        const auto written = put_record(dbi.value(), key, std::nullopt, revision.value());
        if (!written) return written.error();
        return Result<void>();
    }

    CommitResult commit() override
    {
        const auto precondition = check_active();
        if (!precondition) {
            if (m_state == TransactionState::Unknown) {
                return CommitResult{CommitOutcome::Unknown,
                    storage_error(ErrorCode::StorageUnknownCommit,
                                  "MDBX commit outcome remains unknown")};
            }
            return CommitResult{CommitOutcome::Failed, precondition.error()};
        }
        for (const auto& dependency : m_dependencies) {
            const auto pending = m_writes.find(dependency.first);
            if (pending != m_writes.end()) {
                if (pending->second.expected_revision != dependency.second) {
                    return reject_conflict();
                }
            } else {
                const auto dbi = resolve_dbi(dependency.first.name_space);
                if (!dbi) return CommitResult{CommitOutcome::Failed, dbi.error()};
                const auto state = read_state(dbi.value(), dependency.first.name_space,
                                              dependency.first.key);
                if (!state) return CommitResult{CommitOutcome::Failed, state.error()};
                if (state.value().revision != dependency.second) return reject_conflict();
            }
        }
        try {
            m_native->commit();
            m_native.reset();
            m_state = TransactionState::Committed;
            return CommitResult{CommitOutcome::Committed, std::nullopt};
        } catch (const std::exception& exception) {
            m_native.reset();
            m_state = TransactionState::Unknown;
            return CommitResult{CommitOutcome::Unknown,
                storage_error(ErrorCode::StorageUnknownCommit, exception.what())};
        } catch (...) {
            m_native.reset();
            m_state = TransactionState::Unknown;
            return CommitResult{CommitOutcome::Unknown,
                storage_error(ErrorCode::StorageUnknownCommit,
                              "MDBX commit acknowledgement was lost")};
        }
    }

    void rollback() noexcept override
    {
        if (m_state != TransactionState::Active || std::this_thread::get_id() != m_thread) {
            return;
        }
        try {
            if (m_native) m_native->rollback();
        } catch (...) {
        }
        m_native.reset();
        m_state = TransactionState::RolledBack;
    }

private:
    Result<void> check_active() const
    {
        if (std::this_thread::get_id() != m_thread) {
            return storage_error(ErrorCode::StoragePermissionDenied,
                                 "transaction belongs to another thread");
        }
        if (m_state != TransactionState::Active || !m_native || !m_native->handle()) {
            return storage_error(ErrorCode::InvalidState, "transaction is not active");
        }
        return Result<void>();
    }

    Result<void> check_write() const
    {
        const auto active = check_active();
        if (!active) return active.error();
        if (m_mode == TransactionMode::ReadOnly) {
            return storage_error(ErrorCode::StorageReadOnly,
                                 "read-only transaction cannot write");
        }
        return Result<void>();
    }

    Result<ResolvedNamespace> resolve_dbi(std::string_view name_space) const
    {
        if (name_space.empty()) {
            return storage_error(ErrorCode::InvalidArgument, "storage namespace is empty");
        }
        for (const auto& route : namespace_routes) {
            if (name_space == route.logical_name) {
                return ResolvedNamespace{
                    m_owner.dbis[static_cast<std::size_t>(route.physical_namespace)],
                    route.logical_prefix};
            }
        }
        return storage_error(ErrorCode::InvalidArgument,
                             "namespace is outside the MDBX physical profile");
    }

    Result<PhysicalState> read_state(ResolvedNamespace route, std::string_view name_space,
                                     std::string_view key) const
    {
        if (key.empty()) {
            return storage_error(ErrorCode::InvalidArgument, "storage key is empty");
        }
        if (key.size() > m_owner.max_logical_key_size) {
            return storage_error(ErrorCode::StorageRecordTooLarge, "MDBX key exceeds the native limit");
        }
        const RecordKey identity{std::string(name_space), std::string(key)};
        const auto pending = m_writes.find(identity);
        if (pending != m_writes.end()) {
            return PhysicalState{true, pending->second.value, pending->second.resulting_revision};
        }
        const auto physical_key = encode_physical_key(route.logical_prefix, key);
        MDBX_val db_key{const_cast<char*>(physical_key.data()), physical_key.size()};
        MDBX_val db_value{};
        const int rc = mdbx_get(m_native->handle(), route.dbi, &db_key, &db_value);
        if (rc == MDBX_NOTFOUND) return PhysicalState{};
        if (rc != MDBX_SUCCESS) return map_mdbx_error(rc, "reading MDBX record");
        return decode_record(std::string_view(
            static_cast<const char*>(db_value.iov_base), db_value.iov_len));
    }

    Result<void> put_record(ResolvedNamespace route, std::string_view key,
                            const std::optional<StorageBytes>& value,
                            StorageRevision revision)
    {
        const auto encoded = encode_record(value, revision);
        if (!encoded) return terminal_failure(encoded.error());
        const auto physical_key = encode_physical_key(route.logical_prefix, key);
        MDBX_val db_key{const_cast<char*>(physical_key.data()), physical_key.size()};
        MDBX_val db_value{const_cast<char*>(encoded.value().data()), encoded.value().size()};
        const int rc = mdbx_put(m_native->handle(), route.dbi, &db_key, &db_value, MDBX_UPSERT);
        if (rc != MDBX_SUCCESS) return terminal_failure(map_mdbx_error(rc, "writing MDBX record"));
        return Result<void>();
    }

    Result<void> erase_physical(ResolvedNamespace route, std::string_view key)
    {
        const auto physical_key = encode_physical_key(route.logical_prefix, key);
        MDBX_val db_key{const_cast<char*>(physical_key.data()), physical_key.size()};
        const int rc = mdbx_del(m_native->handle(), route.dbi, &db_key, nullptr);
        if (rc == MDBX_NOTFOUND) return Result<void>();
        if (rc != MDBX_SUCCESS) return terminal_failure(map_mdbx_error(rc, "erasing MDBX record"));
        return Result<void>();
    }

    Result<void> terminal_failure(Error error) noexcept
    {
        try {
            if (m_native) m_native->rollback();
        } catch (...) {
        }
        m_native.reset();
        m_state = TransactionState::RolledBack;
        return error;
    }

    CommitResult reject_conflict()
    {
        try {
            if (m_native) m_native->rollback();
        } catch (...) {
        }
        m_native.reset();
        m_state = TransactionState::RolledBack;
        return CommitResult{CommitOutcome::RejectedConflict,
            storage_error(ErrorCode::StorageConflict, "transaction revision conflict")};
    }

    Impl& m_owner;
    TransactionMode m_mode;
    TransactionState m_state = TransactionState::Active;
    std::thread::id m_thread;
    std::unique_ptr<mdbxc::Transaction> m_native;
    std::map<RecordKey, PendingRecord> m_writes;
    std::map<RecordKey, StorageRevision> m_dependencies;
};

MdbxStorageBackend::MdbxStorageBackend(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

MdbxStorageBackend::~MdbxStorageBackend() = default;

Result<std::unique_ptr<MdbxStorageBackend>> MdbxStorageBackend::open(
    MdbxStorageConfig config)
{
    try {
        auto impl = std::make_unique<Impl>(std::move(config));
        const auto initialized = impl->initialize();
        if (!initialized) return initialized.error();
        return std::unique_ptr<MdbxStorageBackend>(
            new MdbxStorageBackend(std::move(impl)));
    } catch (const std::exception& exception) {
        return map_exception(exception);
    } catch (...) {
        return storage_error(ErrorCode::StorageUnavailable,
                             "MDBX backend allocation failed");
    }
}

Result<void> MdbxStorageBackend::close()
{
    if (!m_impl) return Result<void>();
    return m_impl->close();
}

bool MdbxStorageBackend::is_open() const noexcept
{
    return m_impl != nullptr && m_impl->open;
}

Result<std::unique_ptr<ITransaction>> MdbxStorageBackend::begin_transaction(
    TransactionMode mode)
{
    if (!is_open()) {
        return storage_error(ErrorCode::StorageUnavailable,
                             "MDBX backend is closed");
    }
    if (m_impl->stored_schema.migration_state != MigrationState::Stable) {
        return storage_error(ErrorCode::StorageSchemaFailure,
                             "MDBX schema is not admitted for transactions");
    }
    try {
        return std::unique_ptr<ITransaction>(
            new Impl::Transaction(*m_impl, mode));
    } catch (const std::exception& exception) {
        return map_exception(exception);
    } catch (...) {
        return storage_error(ErrorCode::StorageUnavailable,
                             "MDBX transaction creation failed");
    }
}

Result<StorageSchema> MdbxStorageBackend::schema() const
{
    if (!is_open()) {
        return storage_error(ErrorCode::StorageUnavailable,
                             "MDBX backend is closed");
    }
    return m_impl->stored_schema;
}

Result<void> MdbxStorageBackend::validate_schema(const StorageSchema& expected) const
{
    if (!is_open()) {
        return storage_error(ErrorCode::StorageUnavailable,
                             "MDBX backend is closed");
    }
    if (m_impl->stored_schema == expected
        && m_impl->stored_schema.migration_state == MigrationState::Stable) {
        return Result<void>();
    }
    return storage_error(ErrorCode::StorageSchemaFailure,
                         "MDBX schema is incompatible or not stable");
}

} // namespace imtg
