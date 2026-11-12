#include "imtg/storage/mdbx_storage.hpp"

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <exception>
#include <memory>
#include <string>
#include <thread>

namespace {

imtg::StorageSchema stable_schema()
{
    return imtg::StorageSchema{1, {{"meta", 1}, {"sessions", 1},
                                   {"processed_updates", 1}, {"render_intents", 1},
                                   {"outbox", 1}, {"event_journal", 1}}};
}

std::filesystem::path test_path(const char* suffix)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path()
        / (std::string("imtg_mdbx_") + suffix + "_" + std::to_string(stamp));
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

} // namespace

int main()
{
    test_persistence_and_read_your_writes();
    test_snapshot_and_atomic_records();
    test_tombstones_cas_and_scans();
    test_schema_gate_and_read_only();
    test_thread_ownership();
    return 0;
}
