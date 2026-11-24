#include "imtg/storage/mdbx_storage.hpp"
#include "imtg/reconciliation/outbox.hpp"

#include "test_support.hpp"

#include <mdbx.h>

#include <atomic>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "allocation_diagnostic.hpp"

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace allocation_failure {

thread_local std::size_t remaining = (std::numeric_limits<std::size_t>::max)();
thread_local bool triggered = false;

bool fail_now(std::size_t size) noexcept
{
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
    // Debug iterator proxies also allocate inside noexcept string construction
    // and moves. Those failures terminate by contract, rather than propagate.
    // Keep iterator diagnostics enabled; sweep recoverable larger allocations.
    if (!allocation_diagnostic::enabled && size <= sizeof(std::_Container_proxy)) return false;
#else
    (void)size;
#endif
    if (remaining == (std::numeric_limits<std::size_t>::max)()) return false;
    if (remaining != 0) {
        --remaining;
        return false;
    }
    // Fail once so rollback and the caller's diagnostics can allocate normally.
    remaining = (std::numeric_limits<std::size_t>::max)();
    triggered = true;
    return true;
}

} // namespace allocation_failure

void* operator new(std::size_t size)
{
    if (allocation_failure::fail_now(size)) {
#if defined(_MSC_VER)
        if (allocation_diagnostic::enabled) {
            allocation_diagnostic::out("DIAGNOSTIC_INJECT\n", 18);
            allocation_diagnostic::stack();
        }
#endif
        throw std::bad_alloc();
    }
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {

class AllocationFailure {
public:
    explicit AllocationFailure(std::size_t index) noexcept
    {
        allocation_failure::triggered = false;
        allocation_failure::remaining = index;
    }

    ~AllocationFailure()
    {
        allocation_failure::remaining = (std::numeric_limits<std::size_t>::max)();
    }

    bool triggered() const noexcept { return allocation_failure::triggered; }
};

// I/O must not consume an allocation selected by the failure sweep.
void allocation_progress(const char* operation, std::size_t index, const char* phase)
{
    std::cerr << "[ allocation ] operation=" << operation << " index=" << index
              << " phase=" << phase << std::endl;
}

void run_test(const char* name, void (*test)())
{
    std::cerr << "[ run ] " << name << std::endl;
    test();
    std::cerr << "[ passed ] " << name << std::endl;
}

imtg::StorageSchema stable_schema()
{
    return imtg::StorageSchema{1, {{"meta", 1}, {"sessions", 1},
                                   {"processed_updates", 1}, {"render_intents", 1},
                                   {"outbox", 1}, {"event_journal", 1}}};
}

std::filesystem::path test_path(const char* suffix)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::current_path() / "mdbx-test-data";
    std::filesystem::create_directories(directory);
    return directory / (std::string("imtg_mdbx_") + suffix + "_" + std::to_string(stamp));
}

std::unique_ptr<imtg::MdbxStorageBackend> open_backend(
    const std::filesystem::path& path, imtg::StorageSchema schema = stable_schema(),
    bool read_only = false)
{
    imtg::MdbxStorageConfig config;
    config.path = path.string();
    config.schema = std::move(schema);
    config.read_only = read_only;
    auto opened = imtg::MdbxStorageBackend::open(std::move(config));
    CHECK(opened);
    return std::move(opened).value();
}

imtg::UnitOfWork begin(imtg::IStorageBackend& backend,
                       imtg::TransactionMode mode = imtg::TransactionMode::ReadWrite)
{
    auto work = imtg::UnitOfWork::begin(backend, mode);
    CHECK(work);
    return std::move(work).value();
}

void test_persistence_and_read_your_writes()
{
    const auto path = test_path("persistence");
    {
        auto backend = open_backend(path);
        auto work = begin(*backend);
        const auto created = work.transaction()->create_if_absent(
            "sessions", "session-1", "pending");
        CHECK(created && created.value() == 1);
        const auto rewritten = work.transaction()->write(
            "sessions", "session-1", "ready", created.value());
        CHECK(rewritten && rewritten.value() == 1);
        const auto visible = work.transaction()->read("sessions", "session-1");
        CHECK(visible && visible.value()->value == "ready"
              && visible.value()->revision == 1);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    {
        auto backend = open_backend(path);
        auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto restored = work.transaction()->read("sessions", "session-1");
        CHECK(restored && restored.value()->value == "ready"
              && restored.value()->revision == 1);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    std::filesystem::remove(path);
}

void test_snapshot_and_atomic_records()
{
    const auto path = test_path("snapshot");
    auto backend = open_backend(path);
    {
        auto initial = begin(*backend);
        CHECK(initial.transaction()->create_if_absent("sessions", "snapshot", "old"));
        CHECK(initial.commit().outcome == imtg::CommitOutcome::Committed);
    }
    auto snapshot = begin(*backend, imtg::TransactionMode::ReadOnly);
    std::exception_ptr writer_failure;
    std::thread writer_thread([&] {
        try {
            auto writer = begin(*backend);
            CHECK(writer.transaction()->write("sessions", "snapshot", "new", 1));
            CHECK(writer.transaction()->create_if_absent(
                "processed_updates", "update-1", "seen"));
            CHECK(writer.transaction()->create_if_absent(
                "render_intents", "intent-1", "render"));
            CHECK(writer.transaction()->create_if_absent("outbox", "effect-1", "effect"));
            CHECK(writer.commit().outcome == imtg::CommitOutcome::Committed);
        } catch (...) {
            writer_failure = std::current_exception();
        }
    });
    writer_thread.join();
    if (writer_failure) std::rethrow_exception(writer_failure);
    const auto old = snapshot.transaction()->read("sessions", "snapshot");
    CHECK(old && old.value()->value == "old" && old.value()->revision == 1);
    CHECK(snapshot.commit().outcome == imtg::CommitOutcome::Committed);

    auto current = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto updated = current.transaction()->read("sessions", "snapshot");
    const auto processed = current.transaction()->read("processed_updates", "update-1");
    const auto intent = current.transaction()->read("render_intents", "intent-1");
    const auto effect = current.transaction()->read("outbox", "effect-1");
    CHECK(updated && updated.value()->value == "new" && updated.value()->revision == 2);
    CHECK(processed && intent && effect);
    CHECK(current.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_tombstones_cas_and_scans()
{
    const auto path = test_path("tombstone");
    auto backend = open_backend(path);
    auto create = begin(*backend);
    CHECK(create.transaction()->create_if_absent("sessions", "aba", "one"));
    CHECK(create.commit().outcome == imtg::CommitOutcome::Committed);

    auto erase = begin(*backend);
    CHECK(erase.transaction()->erase("sessions", "aba", 1));
    CHECK(erase.commit().outcome == imtg::CommitOutcome::Committed);

    auto recreate = begin(*backend);
    const auto generation = recreate.transaction()->create_if_absent(
        "sessions", "aba", "three");
    CHECK(generation && generation.value() == 3);
    CHECK(recreate.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(*backend);
    const auto rejected = stale.transaction()->write("sessions", "aba", "stale", 1);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageConflict);
    stale.rollback();

    auto scan_writer = begin(*backend);
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-a", "a"));
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-b", "b"));
    CHECK(scan_writer.transaction()->erase("sessions", "scan-b", 1));
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-c", "c"));
    const auto first = scan_writer.transaction()->scan("sessions", {}, 1);
    CHECK(first && first.value().size() == 1 && first.value().front().key == "aba");
    const auto second = scan_writer.transaction()->scan(
        "sessions", first.value().front().key, 10);
    CHECK(second && second.value().size() == 2
          && second.value().front().key == "scan-a"
          && second.value().back().key == "scan-c");
    scan_writer.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_allocation_failure_rolls_back_staging()
{
    enum class Operation {
        CreateAbsent, CreateTombstone, WriteStored, WritePending,
        EraseStored, EraseCreated, EraseRecreated,
    };
    const char* const operation_names[] = {
        "create_absent", "create_tombstone", "write_stored", "write_pending",
        "erase_stored", "erase_created", "erase_recreated"};
    const std::string payload(1024, 'x');
    for (const auto operation : {Operation::CreateAbsent, Operation::CreateTombstone,
                                Operation::WriteStored, Operation::WritePending,
                                Operation::EraseStored, Operation::EraseCreated,
                                Operation::EraseRecreated}) {
        const auto* operation_name = operation_names[static_cast<std::size_t>(operation)];
        std::cerr << "[ allocation ] operation=" << operation_name << " begin" << std::endl;
        const auto path = test_path("allocation-failure");
        auto backend = open_backend(path);
        const bool stored = operation == Operation::WriteStored
            || operation == Operation::WritePending || operation == Operation::EraseStored;
        const bool tombstone = operation == Operation::CreateTombstone
            || operation == Operation::EraseRecreated;
        if (stored || tombstone) {
            auto seed = begin(*backend);
            CHECK(seed.transaction()->create_if_absent("sessions", "target", "original"));
            CHECK(seed.commit().outcome == imtg::CommitOutcome::Committed);
        }
        if (tombstone) {
            auto seed = begin(*backend);
            CHECK(seed.transaction()->erase("sessions", "target", 1));
            CHECK(seed.commit().outcome == imtg::CommitOutcome::Committed);
        }
        bool reached_success = false;
        // Sweep allocations including encoding after shadow-state mutation.
        // MSVC iterator-debug mode excludes tiny noexcept proxy allocations above.
        for (std::size_t index =
#if defined(_MSC_VER)
                 allocation_diagnostic::enabled ? 4 :
#endif
                 0; index != 64; ++index) {
            allocation_progress(operation_name, index, "setup");
            auto work = begin(*backend);
            CHECK(work.transaction()->create_if_absent("outbox", "must-rollback", "staged"));
            imtg::StorageRevision visible_revision = 1;
            if (operation == Operation::WritePending) {
                CHECK(work.transaction()->write("sessions", "target", payload, 1));
                visible_revision = 2;
            } else if (operation == Operation::EraseCreated
                       || operation == Operation::EraseRecreated) {
                const auto created = work.transaction()->create_if_absent(
                    "sessions", "target", payload);
                CHECK(created);
                visible_revision = created.value();
            }
            bool caught = false;
            bool injected = false;
            bool succeeded = false;
            allocation_progress(operation_name, index, "inject");
            {
                AllocationFailure failure(index);
                try {
                    switch (operation) {
                    case Operation::CreateAbsent:
                    case Operation::CreateTombstone:
                        succeeded = static_cast<bool>(work.transaction()->create_if_absent(
                            "sessions", "target", payload));
                        break;
                    case Operation::WriteStored:
                    case Operation::WritePending:
                        succeeded = static_cast<bool>(work.transaction()->write(
                            "sessions", "target", payload, visible_revision));
                        break;
                    default:
                        succeeded = static_cast<bool>(work.transaction()->erase(
                            "sessions", "target", visible_revision));
                        break;
                    }
                } catch (const std::bad_alloc&) {
                    caught = true;
                }
                injected = failure.triggered();
            }
            allocation_progress(operation_name, index, "returned");
            if (!injected) {
                CHECK(succeeded && !caught && index != 0);
                work.rollback();
                reached_success = true;
                break;
            }
            CHECK(caught);
            CHECK(work.transaction()->state() == imtg::TransactionState::RolledBack);
            const auto point = work.transaction()->read("sessions", "target");
            CHECK(!point && point.error().code == imtg::ErrorCode::InvalidState);
            const auto page = work.transaction()->scan("sessions", {}, 10);
            CHECK(!page && page.error().code == imtg::ErrorCode::InvalidState);
            CHECK(work.commit().outcome == imtg::CommitOutcome::Failed);
            allocation_progress(operation_name, index, "verify");
            auto verify = begin(*backend, imtg::TransactionMode::ReadOnly);
            const auto target = verify.transaction()->read("sessions", "target");
            CHECK(target && static_cast<bool>(target.value()) == stored);
            if (stored) {
                CHECK(target.value()->value == "original" && target.value()->revision == 1);
            }
            CHECK(verify.transaction()->watch("sessions", "target", stored ? 1 : tombstone ? 2 : 0));
            const auto prior = verify.transaction()->read("outbox", "must-rollback");
            CHECK(prior && !prior.value());
            const auto visible = verify.transaction()->scan("sessions", {}, 10);
            CHECK(visible && visible.value().size() == (stored ? 1 : 0));
            CHECK(verify.commit().outcome == imtg::CommitOutcome::Committed);
            allocation_progress(operation_name, index, "verified");
        }
        CHECK(reached_success);
        CHECK(backend->close());
        std::filesystem::remove(path);
    }
}

void test_schema_gate_and_read_only()
{
    const auto migration_path = test_path("migration");
    auto migrating_schema = stable_schema();
    migrating_schema.migration_state = imtg::MigrationState::Migrating;
    auto migrating = open_backend(migration_path, migrating_schema);
    CHECK(!migrating->begin_transaction(imtg::TransactionMode::ReadOnly));
    CHECK(!migrating->validate_schema(migrating_schema));
    CHECK(migrating->close());
    std::filesystem::remove(migration_path);

    const auto path = test_path("readonly");
    auto writable = open_backend(path);
    auto work = begin(*writable);
    CHECK(work.transaction()->create_if_absent("sessions", "read-only", "value"));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(writable->close());

    auto read_only = open_backend(path, stable_schema(), true);
    CHECK(!read_only->begin_transaction(imtg::TransactionMode::ReadWrite));
    auto incompatible = stable_schema();
    incompatible.profile_version = 2;
    CHECK(!read_only->validate_schema(incompatible));
    auto read_work = begin(*read_only, imtg::TransactionMode::ReadOnly);
    const auto forbidden = read_work.transaction()->write(
        "sessions", "read-only", "changed", 1);
    CHECK(!forbidden && forbidden.error().code == imtg::ErrorCode::StorageReadOnly);
    read_work.rollback();
    CHECK(read_only->close());
    std::filesystem::remove(path);
}

void test_thread_ownership()
{
    const auto path = test_path("thread");
    auto backend = open_backend(path);
    auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
    std::atomic<bool> rejected{false};
    std::thread other([&] {
        const auto result = work.transaction()->read("sessions", "foreign");
        rejected = !result
            && result.error().code == imtg::ErrorCode::StoragePermissionDenied;
    });
    other.join();
    CHECK(rejected.load());
    work.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_namespace_isolation_and_binary_pagination()
{
    const auto path = test_path("namespaces");
    const std::array<std::string_view, 11> namespaces = {
        "meta", "sessions", "processed_updates", "render_intents",
        "render_intent_claims", "render_intent_generations", "outbox_effects",
        "outbox_dispatch_targets", "applied_targets", "event_journal", "outbox"};
    const std::vector<std::string> keys = {
        std::string("\0", 1), std::string("a\0", 2), std::string("a\0b", 3),
        std::string("\x80", 1), std::string("\xff", 1)};
    {
        auto backend = open_backend(path);
        auto work = begin(*backend);
        for (const auto name_space : namespaces) {
            for (const auto& key : keys) {
                CHECK(work.transaction()->create_if_absent(name_space, key, name_space));
                CHECK(work.transaction()->watch(name_space, key, 1));
            }
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        auto mutations = begin(*backend);
        for (const auto name_space : namespaces) {
            CHECK(mutations.transaction()->write(name_space, keys[0], "changed", 1));
            CHECK(mutations.transaction()->watch(name_space, keys[0], 2));
            CHECK(mutations.transaction()->erase(name_space, keys[1], 1));
        }
        CHECK(mutations.commit().outcome == imtg::CommitOutcome::Committed);
        auto recreate = begin(*backend);
        for (const auto name_space : namespaces) {
            const auto created = recreate.transaction()->create_if_absent(name_space, keys[1], name_space);
            CHECK(created && created.value() == 3);
            CHECK(!recreate.transaction()->write(name_space, keys[0], "stale", 1));
            CHECK(!recreate.transaction()->watch(name_space, keys[0], 1));
        }
        CHECK(recreate.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    auto backend = open_backend(path);
    auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
    for (const auto name_space : namespaces) {
        std::string continuation;
        for (std::size_t index = 0; index != keys.size(); ++index) {
            const auto page = work.transaction()->scan(name_space, continuation, 1);
            CHECK(page && page.value().size() == 1);
            const auto& row = page.value().front();
            CHECK(row.key == keys[index]);
            CHECK(row.record.value == (index == 0 ? "changed" : name_space));
            CHECK(row.record.revision == (index == 0 ? 2 : index == 1 ? 3 : 1));
            continuation = row.key;
        }
        const auto end = work.transaction()->scan(name_space, continuation, 1);
        CHECK(end && end.value().empty());
        const auto absent_cursor = work.transaction()->scan(name_space, "a", 1);
        CHECK(absent_cursor && absent_cursor.value().front().key == keys[1]);
    }
    CHECK(!work.transaction()->read("unregistered", "key"));
    CHECK(!work.transaction()->scan("unregistered", {}, 1));
    CHECK(!work.transaction()->watch("unregistered", "key", 0));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    auto invalid = begin(*backend);
    CHECK(!invalid.transaction()->create_if_absent("unregistered", "key", "value"));
    CHECK(!invalid.transaction()->write("unregistered", "key", "value", 1));
    CHECK(!invalid.transaction()->erase("unregistered", "key", 1));
    const std::string oversized_key(65536, 'x');
    const auto oversized = invalid.transaction()->read("sessions", oversized_key);
    CHECK(!oversized && oversized.error().code == imtg::ErrorCode::StorageRecordTooLarge);
    CHECK(!invalid.transaction()->create_if_absent("sessions", oversized_key, "value"));
    CHECK(!invalid.transaction()->watch("sessions", oversized_key, 0));
    CHECK(!invalid.transaction()->scan("sessions", oversized_key, 1));
    invalid.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_public_meta_and_sparse_scans()
{
    const auto path = test_path("meta-sparse");
    auto backend = open_backend(path);
    {
        auto work = begin(*backend);
        const auto empty = work.transaction()->scan("meta", {}, 1);
        CHECK(empty && empty.value().empty());
        CHECK(work.transaction()->create_if_absent("meta", "imtg/schema/v1", "public"));
        for (int index = 0; index != 256; ++index) {
            CHECK(work.transaction()->create_if_absent("meta", "dead-" + std::to_string(index), "deleted"));
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    {
        auto work = begin(*backend);
        for (int index = 0; index != 256; ++index) {
            CHECK(work.transaction()->erase("meta", "dead-" + std::to_string(index), 1));
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    for (int pass = 0; pass != 2; ++pass) {
        auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto page = work.transaction()->scan("meta", {}, 1);
        CHECK(page && page.value().size() == 1);
        CHECK(page.value().front().key == "imtg/schema/v1"
              && page.value().front().record.value == "public");
        const auto schema = backend->schema();
        CHECK(schema && schema.value() == stable_schema());
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
        if (pass == 0) backend = open_backend(path);
    }
    std::filesystem::remove(path);
}

void test_close_with_live_transactions()
{
    const auto path = test_path("close");
    auto backend = open_backend(path);
    auto work = begin(*backend);
    CHECK(!backend->close());
    CHECK(backend->is_open());
    CHECK(work.transaction()->create_if_absent("sessions", "live", "value"));
    work.rollback();
    CHECK(backend->close());
    backend = open_backend(path);
    auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto absent = read.transaction()->read("sessions", "live");
    CHECK(absent && !absent.value());
    CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_unprefixed_draft_is_rejected()
{
    const auto path = test_path("legacy");
    MDBX_env* environment = nullptr;
    CHECK(mdbx_env_create(&environment) == MDBX_SUCCESS);
    CHECK(mdbx_env_set_maxdbs(environment, 8) == MDBX_SUCCESS);
    CHECK(mdbx_env_open(environment, path.string().c_str(), MDBX_NOSUBDIR, 0664) == MDBX_SUCCESS);
    MDBX_txn* native = nullptr;
    CHECK(mdbx_txn_begin(environment, nullptr, MDBX_TXN_READWRITE, &native) == MDBX_SUCCESS);
    MDBX_dbi meta{};
    CHECK(mdbx_dbi_open(native, "meta", MDBX_CREATE, &meta) == MDBX_SUCCESS);
    std::string key = "imtg/schema/v1";
    std::string value = "unreleased unprefixed schema";
    MDBX_val physical_key{key.data(), key.size()};
    MDBX_val physical_value{value.data(), value.size()};
    CHECK(mdbx_put(native, meta, &physical_key, &physical_value, MDBX_UPSERT) == MDBX_SUCCESS);
    CHECK(mdbx_txn_commit(native) == MDBX_SUCCESS);
    CHECK(mdbx_env_close(environment) == MDBX_SUCCESS);
    imtg::MdbxStorageConfig config;
    config.path = path.string();
    const auto rejected = imtg::MdbxStorageBackend::open(config);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageSchemaFailure);
    std::filesystem::remove(path);
}

void test_repositories_tx1_tx2_delivery_and_reopen()
{
    const auto path = test_path("repositories");
    auto backend = open_backend(path);
    const imtg::ConversationKey conversation{imtg::BotId{7}, imtg::ChatId{42}, imtg::Chat{}};
    const imtg::UpdateIdentity update{conversation.bot_id, 19};
    const imtg::ClockTime now{std::chrono::seconds(100)};
    imtg::SessionRepository sessions;
    imtg::ProcessedUpdateRepository processed;
    imtg::RenderIntentRepository intents;
    imtg::RenderIntentClaimRepository claims;
    imtg::DesiredGenerationRepository generations;
    imtg::OutboxRepository outbox;
    imtg::AppliedTargetRepository applied;
    imtg::Fsm fsm;
    CHECK(fsm.add_transition({"start", 1}, {"begin", 1}, {"ready", 1}));
    imtg::SessionRecord initial;
    initial.conversation = conversation;
    initial.state = {"start", 1};
    initial.screen = {"start", 1};
    initial.created_at = now;
    initial.last_transition_at = now;
    {
        imtg::AtomicUpdateProcessor processor(*backend, sessions, processed, intents);
        const auto failed = processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"missing", 1}}, now, initial);
        CHECK(failed.outcome == imtg::UpdateProcessOutcome::Failed);
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto absent_session = sessions.load(conversation, *read.transaction());
        const auto absent_marker = read.transaction()->read("processed_updates", update.encode());
        const auto absent_intents = read.transaction()->scan("render_intents", {}, 10);
        CHECK(absent_session && !absent_session.value());
        CHECK(absent_marker && !absent_marker.value());
        CHECK(absent_intents && absent_intents.value().empty());
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    imtg::UpdateProcessResult tx1;
    {
        imtg::AtomicUpdateProcessor processor(*backend, sessions, processed, intents);
        tx1 = processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"begin", 1}}, now, initial);
        CHECK(tx1.outcome == imtg::UpdateProcessOutcome::Committed);
        CHECK(tx1.session && tx1.render_intent);
        CHECK(processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"begin", 1}}, now).outcome == imtg::UpdateProcessOutcome::Duplicate);
    }
    imtg::RenderIntentClaim intent_claim;
    imtg::DesiredGeneration generation;
    {
        auto work = begin(*backend);
        const auto marker = generations.ensure(conversation.encode(), *work.transaction());
        const auto claimed = claims.claim_render_intents(
            1, now, std::chrono::hours(1), {}, *work.transaction());
        CHECK(marker && claimed && claimed.value().claims.size() == 1);
        generation = marker.value();
        intent_claim = claimed.value().claims.front();
        CHECK(intent_claim.id == *tx1.render_intent);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    const auto target = imtg::make_logical_target_identity(conversation, "body");
    CHECK(target);
    imtg::ReconciliationInput input{*tx1.render_intent, target.value(), imtg::EffectKind::Edit,
        "body", tx1.session->revision, 0, imtg::RenderPolicy::OnChange, 1, "ready payload"};
    input.conversation = conversation;
    input.session_revision = tx1.session->revision;
    input.desired_generation = generation.value;
    input.desired_generation_row_revision = generation.row_revision;
    input.context = imtg::ReconciliationContext{
        *tx1.render_intent, conversation, tx1.session->revision, tx1.session->revision,
        generation.value, generation.row_revision, target.value(), imtg::EffectKind::Edit,
        "body", 0, 0, imtg::RenderPolicy::OnChange,
        intent_claim.claim_token, intent_claim.row_revision};
    const auto plan = imtg::ReconciliationPlanner{}.build(input);
    CHECK(plan && !plan.value().no_op);
    {
        imtg::AtomicReconciliation publisher(*backend, claims, outbox);
        auto work = begin(*backend);
        CHECK(publisher.stage(plan.value(), *tx1.render_intent, intent_claim.claim_token,
                              intent_claim.row_revision, now, *work.transaction()));
        work.rollback();
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto absent = outbox.load(plan.value().effect.id, *read.transaction());
        const auto unchanged = claims.inspect(*tx1.render_intent, now, *read.transaction());
        CHECK(absent && !absent.value());
        CHECK(unchanged && unchanged.value()
              && unchanged.value()->state == imtg::RenderIntentLifecycleState::Claimed
              && unchanged.value()->row_revision == intent_claim.row_revision);
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
        const auto published = publisher.publish(plan.value(), *tx1.render_intent,
            intent_claim.claim_token, intent_claim.row_revision, now);
        CHECK(published.outcome == imtg::ReconciliationOutcome::Planned && published.effect);
    }
    imtg::OutboxEffect admitted;
    {
        auto work = begin(*backend);
        const auto claimed = outbox.claim_outbox_effects(
            1, now, std::chrono::hours(1), {}, *work.transaction());
        CHECK(claimed && claimed.value().effects.size() == 1);
        const auto claim = claimed.value().effects.front();
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        auto admission = begin(*backend);
        const auto result = outbox.mark_dispatch_admitted(claim.id, claim.claim_token,
            claim.revision, now, *admission.transaction());
        CHECK(result);
        admitted = result.value();
        CHECK(admission.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    backend = open_backend(path);
    {
        auto read = begin(*backend);
        const auto session = sessions.load(conversation, *read.transaction());
        const auto intent = intents.load(*tx1.render_intent, *read.transaction());
        const auto claim = claims.inspect(*tx1.render_intent, now, *read.transaction());
        const auto marker = generations.load(conversation.encode(), *read.transaction());
        const auto effect = outbox.load(admitted.id, *read.transaction());
        const auto owner = read.transaction()->read("outbox_dispatch_targets", target.value());
        CHECK(session && session.value() && session.value()->state.name == "ready"
              && session.value()->revision == tx1.session->revision);
        CHECK(intent && intent.value() && intent.value()->id == *tx1.render_intent);
        CHECK(claim && claim.value() && claim.value()->state == imtg::RenderIntentLifecycleState::Planned
              && claim.value()->claim_token == intent_claim.claim_token);
        CHECK(marker && marker.value().value == generation.value
              && marker.value().row_revision == generation.row_revision);
        CHECK(effect && effect.value() && effect.value()->state == imtg::OutboxEffectState::DispatchAdmitted
              && effect.value()->payload == "ready payload"
              && effect.value()->revision == admitted.revision);
        CHECK(owner && owner.value() && owner.value()->value.substr(0, 3) == "DT1");
        CHECK(owner.value()->value.find(admitted.id.value) != std::string::npos);
        const auto duplicate = processed.record_if_new(update, *read.transaction());
        CHECK(duplicate && !duplicate.value());
        const auto acknowledged = outbox.acknowledge_success(admitted.id, admitted.claim_token,
            admitted.revision, now, "telegram-message-42", applied, *read.transaction());
        CHECK(acknowledged && acknowledged.value().state == imtg::OutboxEffectState::Succeeded);
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    backend = open_backend(path);
    {
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto effect = outbox.load(admitted.id, *read.transaction());
        const auto confirmed = applied.load(target.value(), *read.transaction());
        const auto owner = read.transaction()->read("outbox_dispatch_targets", target.value());
        CHECK(effect && effect.value() && effect.value()->state == imtg::OutboxEffectState::Succeeded);
        CHECK(confirmed && confirmed.value()
              && confirmed.value()->confirmed_applied_revision == tx1.session->revision
              && confirmed.value()->physical_identity == "telegram-message-42"
              && confirmed.value()->last_confirmed_effect == admitted.id);
        CHECK(owner && owner.value() && owner.value()->revision == 2);
        CHECK(owner.value()->value == std::string("DT1") + std::string(16, '\0'));
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    std::filesystem::remove(path);
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Capture native assertion reports instead of opening an unattended dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
#if defined(_MSC_VER)
    allocation_diagnostic::install(false);
    if (argc == 2 && std::string_view(argv[1]) == "--diagnose-create") {
        std::fprintf(stderr, "MSC=%d ITERATOR=%d STL=%d proxy=%zu default_noexcept=%d\n",
            _MSC_FULL_VER, _ITERATOR_DEBUG_LEVEL, _MSVC_STL_VERSION,
            sizeof(std::_Container_proxy), noexcept(std::string{}));
        allocation_diagnostic::install();
        test_allocation_failure_rolls_back_staging();
        return 0;
    }
#else
    (void)argc;
    (void)argv;
#endif
    try {
        run_test("persistence_and_read_your_writes", test_persistence_and_read_your_writes);
        run_test("snapshot_and_atomic_records", test_snapshot_and_atomic_records);
        run_test("tombstones_cas_and_scans", test_tombstones_cas_and_scans);
        run_test("allocation_failure_rolls_back_staging", test_allocation_failure_rolls_back_staging);
        run_test("schema_gate_and_read_only", test_schema_gate_and_read_only);
        run_test("thread_ownership", test_thread_ownership);
        run_test("namespace_isolation_and_binary_pagination", test_namespace_isolation_and_binary_pagination);
        run_test("public_meta_and_sparse_scans", test_public_meta_and_sparse_scans);
        run_test("close_with_live_transactions", test_close_with_live_transactions);
        run_test("unprefixed_draft_is_rejected", test_unprefixed_draft_is_rejected);
        run_test("repositories_tx1_tx2_delivery_and_reopen", test_repositories_tx1_tx2_delivery_and_reopen);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
