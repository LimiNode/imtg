#include "imtg/storage/storage.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

namespace imtg {

namespace {

Error storage_error(ErrorCode code, const char* message)
{
    return Error{code, message};
}

} // namespace

Result<StorageRevision> next_storage_revision(StorageRevision current)
{
    if (current == std::numeric_limits<StorageRevision>::max()) {
        return storage_error(ErrorCode::StorageRevisionExhausted,
                             "storage revision space is exhausted");
    }
    return current + 1;
}

UnitOfWork::~UnitOfWork()
{
    if (m_transaction != nullptr && m_transaction->state() == TransactionState::Active) {
        m_transaction->rollback();
    }
}

UnitOfWork& UnitOfWork::operator=(UnitOfWork&& other) noexcept
{
    if (this == &other) return *this;
    if (m_transaction != nullptr && m_transaction->state() == TransactionState::Active) {
        m_transaction->rollback();
    }
    m_transaction = std::move(other.m_transaction);
    return *this;
}

Result<UnitOfWork> UnitOfWork::begin(IStorageBackend& backend, TransactionMode mode)
{
    auto transaction = backend.begin_transaction(mode);
    if (!transaction) {
        return transaction.error();
    }
    return UnitOfWork(std::move(transaction).value());
}

CommitResult UnitOfWork::commit()
{
    if (!m_transaction) {
        return CommitResult{CommitOutcome::Failed,
                            storage_error(ErrorCode::InvalidState, "unit of work is empty")};
    }
    return m_transaction->commit();
}

void UnitOfWork::rollback() noexcept
{
    if (m_transaction != nullptr && m_transaction->state() == TransactionState::Active) {
        m_transaction->rollback();
    }
}

class InMemoryStorageBackend::Transaction final : public ITransaction {
public:
    using RecordMap = std::unordered_map<RecordKey, StoredRecord, RecordKeyHash>;

    Transaction(InMemoryStorageBackend& backend, TransactionMode mode, RecordMap snapshot,
                std::thread::id owner)
        : m_backend(backend), m_mode(mode), m_snapshot(std::move(snapshot)), m_owner(owner)
    {
    }

    TransactionMode mode() const noexcept override { return m_mode; }
    TransactionState state() const noexcept override { return m_state; }

    Result<std::optional<StorageRecord>> read(std::string_view name_space,
                                              std::string_view key) override
    {
        const auto precondition = check_active();
        if (!precondition) return precondition.error();
        const auto identity = make_key(name_space, key);
        if (!identity) return identity.error();
        const auto pending = m_writes.find(identity.value());
        if (pending != m_writes.end()) {
            if (!pending->second.value.has_value()) return std::optional<StorageRecord>{};
            return std::optional<StorageRecord>{StorageRecord{
                *pending->second.value, pending->second.resulting_revision}};
        }
        const auto found = m_snapshot.find(identity.value());
        if (found == m_snapshot.end() || !found->second.value.has_value()) {
            return std::optional<StorageRecord>{};
        }
        return std::optional<StorageRecord>{StorageRecord{
            *found->second.value, found->second.revision}};
    }

    Result<void> watch(std::string_view name_space, std::string_view key,
                       StorageRevision expected_revision) override
    {
        const auto precondition = check_active();
        if (!precondition) return precondition.error();
        const auto identity = make_key(name_space, key);
        if (!identity) return identity.error();
        const auto pending = m_writes.find(identity.value());
        const auto found = m_snapshot.find(identity.value());
        const auto visible_revision = pending != m_writes.end()
            ? pending->second.resulting_revision
            : (found == m_snapshot.end() ? StorageRevision{0} : found->second.revision);
        if (visible_revision != expected_revision) {
            return storage_error(ErrorCode::StorageConflict, "read dependency is stale");
        }
        m_dependencies[identity.value()] = expected_revision;
        return Result<void>();
    }

    Result<StorageRevision> create_if_absent(std::string_view name_space,
                                              std::string_view key,
                                              std::string_view value) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto identity = make_key(name_space, key);
        if (!identity) return identity.error();
        const auto visible = visible_state(identity.value());
        if (visible.value.has_value()) {
            return storage_error(ErrorCode::StorageConflict, "record already exists");
        }
        const auto revision = next_storage_revision(base_revision(identity.value()));
        if (!revision) return revision.error();
        const auto prepared = prepare_write();
        if (!prepared) return prepared.error();
        const auto pending = m_writes.find(identity.value());
        if (pending != m_writes.end()) {
            pending->second.value = std::string(value);
            return pending->second.resulting_revision;
        }
        m_writes.emplace(identity.value(), Pending{
            std::string(value), visible.revision, revision.value()});
        return revision.value();
    }

    Result<StorageRevision> write(std::string_view name_space,
                                  std::string_view key,
                                  std::string_view value,
                                  StorageRevision expected_revision) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto identity = make_key(name_space, key);
        if (!identity) return identity.error();
        const auto visible = visible_state(identity.value());
        if (!visible.value.has_value()) {
            return storage_error(ErrorCode::StorageNotFound, "record does not exist");
        }
        if (visible.revision != expected_revision || expected_revision == 0) {
            return storage_error(ErrorCode::StorageConflict, "record revision is stale");
        }
        const auto revision = next_storage_revision(base_revision(identity.value()));
        if (!revision) return revision.error();
        const auto prepared = prepare_write();
        if (!prepared) return prepared.error();
        const auto pending = m_writes.find(identity.value());
        if (pending != m_writes.end()) {
            pending->second.value = std::string(value);
        } else {
            m_writes[identity.value()] = Pending{
                std::string(value), visible.revision, revision.value()};
        }
        return revision.value();
    }

    Result<void> erase(std::string_view name_space,
                       std::string_view key,
                       StorageRevision expected_revision) override
    {
        const auto precondition = check_write();
        if (!precondition) return precondition.error();
        const auto identity = make_key(name_space, key);
        if (!identity) return identity.error();
        const auto visible = visible_state(identity.value());
        if (!visible.value.has_value()) {
            return storage_error(ErrorCode::StorageNotFound, "record does not exist");
        }
        if (visible.revision != expected_revision || expected_revision == 0) {
            return storage_error(ErrorCode::StorageConflict, "record revision is stale");
        }
        const auto revision = next_storage_revision(base_revision(identity.value()));
        if (!revision) return revision.error();
        const auto prepared = prepare_write();
        if (!prepared) return prepared.error();
        const auto pending = m_writes.find(identity.value());
        if (pending != m_writes.end()) {
            if (!snapshot_has_value(identity.value())) {
                m_writes.erase(pending);
            } else {
                pending->second.value = std::nullopt;
            }
            return Result<void>();
        }
        m_writes[identity.value()] = Pending{
            std::nullopt, visible.revision, revision.value()};
        return Result<void>();
    }

    CommitResult commit() override
    {
        const auto precondition = check_active();
        if (!precondition) {
            if (m_state == TransactionState::Unknown) {
                return CommitResult{CommitOutcome::Unknown,
                                    storage_error(ErrorCode::StorageUnknownCommit,
                                                  "commit outcome remains unknown")};
            }
            return CommitResult{CommitOutcome::Failed, precondition.error()};
        }
        if (m_mode == TransactionMode::ReadOnly && !m_writes.empty()) {
            return failed(storage_error(ErrorCode::StorageReadOnly,
                                         "read-only transaction cannot write"));
        }
        if (m_writes.empty()) {
            // Publication failure injection applies only when there is a write set to publish.
            // Leaving it armed makes the next publishing commit deterministic.
            m_state = TransactionState::Committed;
            return CommitResult{CommitOutcome::Committed, std::nullopt};
        }

        std::lock_guard<std::mutex> lock(m_backend.m_mutex);
        if (m_backend.m_failure_injection.unknown_before_publish) {
            m_backend.m_failure_injection.unknown_before_publish = false;
            m_state = TransactionState::Unknown;
            return CommitResult{CommitOutcome::Unknown,
                                storage_error(ErrorCode::StorageUnknownCommit,
                                              "commit outcome is unknown")};
        }
        if (m_backend.m_failure_injection.fail_before_publish) {
            m_backend.m_failure_injection.fail_before_publish = false;
            m_state = TransactionState::RolledBack;
            return failed(storage_error(ErrorCode::StorageUnavailable,
                                         "commit failed before publication"));
        }
        for (const auto& entry : m_writes) {
            const auto found = m_backend.m_records.find(entry.first);
            const auto current_revision = found == m_backend.m_records.end()
                ? StorageRevision{0} : found->second.revision;
            if (current_revision != entry.second.expected_revision) {
                return conflict();
            }
        }
        for (const auto& dependency : m_dependencies) {
            const auto found = m_backend.m_records.find(dependency.first);
            const auto current_revision = found == m_backend.m_records.end()
                ? StorageRevision{0} : found->second.revision;
            if (current_revision != dependency.second) {
                return conflict();
            }
        }
        try {
            RecordMap next(m_backend.m_records);
            for (const auto& entry : m_writes) {
                next[entry.first] = StoredRecord{
                    entry.second.value, entry.second.resulting_revision};
            }
            m_backend.m_records.swap(next);
        } catch (const std::exception& exception) {
            return failed(storage_error(ErrorCode::StorageUnavailable, exception.what()));
        } catch (...) {
            return failed(storage_error(ErrorCode::StorageUnavailable,
                                         "commit publication preparation failed"));
        }
        if (m_backend.m_failure_injection.unknown_after_publish) {
            m_backend.m_failure_injection.unknown_after_publish = false;
            m_state = TransactionState::Unknown;
            return CommitResult{CommitOutcome::Unknown,
                                storage_error(ErrorCode::StorageUnknownCommit,
                                              "commit acknowledgement was lost")};
        }
        m_state = TransactionState::Committed;
        return CommitResult{CommitOutcome::Committed, std::nullopt};
    }

    void rollback() noexcept override
    {
        if (m_state == TransactionState::Active && std::this_thread::get_id() == m_owner) {
            m_writes.clear();
            m_state = TransactionState::RolledBack;
        }
    }

private:
    struct Pending {
        std::optional<StorageBytes> value;
        StorageRevision expected_revision{};
        StorageRevision resulting_revision{};
    };

    struct VisibleState {
        std::optional<StorageBytes> value;
        StorageRevision revision{};
    };

    Result<void> check_active() const
    {
        if (std::this_thread::get_id() != m_owner) {
            return storage_error(ErrorCode::StoragePermissionDenied,
                                 "transaction belongs to another thread");
        }
        if (m_state != TransactionState::Active) {
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

    Result<void> prepare_write()
    {
        std::lock_guard<std::mutex> lock(m_backend.m_mutex);
        if (m_backend.m_failure_injection.fail_write_set_preparation) {
            m_backend.m_failure_injection.fail_write_set_preparation = false;
            return storage_error(ErrorCode::StorageUnavailable,
                                 "write-set preparation failed");
        }
        return Result<void>();
    }

    Result<RecordKey> make_key(std::string_view name_space, std::string_view key) const
    {
        if (name_space.empty() || key.empty()) {
            return storage_error(ErrorCode::InvalidArgument, "storage identity is empty");
        }
        return RecordKey{std::string(name_space), std::string(key)};
    }

    VisibleState visible_state(const RecordKey& key) const
    {
        const auto pending = m_writes.find(key);
        if (pending != m_writes.end()) {
            return VisibleState{
                pending->second.value, pending->second.resulting_revision};
        }
        const auto found = m_snapshot.find(key);
        if (found == m_snapshot.end()) return VisibleState{};
        return VisibleState{found->second.value, found->second.revision};
    }

    StorageRevision base_revision(const RecordKey& key) const noexcept
    {
        const auto pending = m_writes.find(key);
        if (pending != m_writes.end()) return pending->second.expected_revision;
        const auto found = m_snapshot.find(key);
        return found == m_snapshot.end() ? StorageRevision{0} : found->second.revision;
    }

    bool snapshot_has_value(const RecordKey& key) const noexcept
    {
        const auto found = m_snapshot.find(key);
        return found != m_snapshot.end() && found->second.value.has_value();
    }

    CommitResult failed(Error error)
    {
        m_state = TransactionState::RolledBack;
        return CommitResult{CommitOutcome::Failed, std::move(error)};
    }

    CommitResult conflict()
    {
        m_state = TransactionState::RolledBack;
        return CommitResult{CommitOutcome::RejectedConflict,
                            storage_error(ErrorCode::StorageConflict,
                                          "transaction revision conflict")};
    }

    InMemoryStorageBackend& m_backend;
    TransactionMode m_mode;
    TransactionState m_state = TransactionState::Active;
    RecordMap m_snapshot;
    std::unordered_map<RecordKey, Pending, RecordKeyHash> m_writes;
    std::unordered_map<RecordKey, StorageRevision, RecordKeyHash> m_dependencies;
    std::thread::id m_owner;
};

std::size_t InMemoryStorageBackend::RecordKeyHash::operator()(
    const RecordKey& value) const noexcept
{
    const auto first = std::hash<std::string>{}(value.name_space);
    const auto second = std::hash<std::string>{}(value.key);
    return first ^ (second + static_cast<std::size_t>(0x9e3779b9U) + (first << 6) + (first >> 2));
}

InMemoryStorageBackend::InMemoryStorageBackend(StorageSchema schema)
    : m_schema(std::move(schema))
{
}

InMemoryStorageBackend::~InMemoryStorageBackend() = default;

Result<std::unique_ptr<ITransaction>> InMemoryStorageBackend::begin_transaction(
    TransactionMode mode)
{
    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_schema.migration_state != MigrationState::Stable) {
            return storage_error(ErrorCode::StorageSchemaFailure,
                                 "storage schema is not ready for transactions");
        }
        auto snapshot = std::unordered_map<RecordKey, StoredRecord, RecordKeyHash>(m_records);
        return std::unique_ptr<ITransaction>(
            new Transaction(*this, mode, std::move(snapshot), std::this_thread::get_id()));
    } catch (const std::exception& exception) {
        return storage_error(ErrorCode::StorageUnavailable, exception.what());
    } catch (...) {
        return storage_error(ErrorCode::StorageUnavailable, "transaction allocation failed");
    }
}

Result<StorageSchema> InMemoryStorageBackend::schema() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_schema;
}

Result<void> InMemoryStorageBackend::validate_schema(const StorageSchema& expected) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_schema == expected && m_schema.migration_state == MigrationState::Stable) {
        return Result<void>();
    }
    return storage_error(ErrorCode::StorageSchemaFailure, "storage schema is incompatible");
}

void InMemoryStorageBackend::set_failure_injection(InMemoryFailureInjection injection)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_failure_injection = injection;
}

std::size_t InMemoryStorageBackend::record_count() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<std::size_t>(std::count_if(
        m_records.begin(), m_records.end(), [](const auto& entry) {
            return entry.second.value.has_value();
        }));
}

} // namespace imtg
