#include "imtg/storage/storage.hpp"

#include "test_support.hpp"

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace {

using BackendFactory = std::function<std::unique_ptr<imtg::IStorageBackend>(imtg::StorageSchema)>;

struct BackendFixture {
    BackendFactory create_backend;
    bool supports_multiple_active_writers = false;
};

imtg::StorageSchema stable_schema()
{
    return imtg::StorageSchema{1, {{"meta", 1}, {"sessions", 1}}};
}

imtg::UnitOfWork begin(imtg::IStorageBackend& backend,
                       imtg::TransactionMode mode = imtg::TransactionMode::ReadWrite)
{
    auto result = imtg::UnitOfWork::begin(backend, mode);
    CHECK(result);
    return std::move(result).value();
}

imtg::StorageRevision create_record(imtg::IStorageBackend& backend,
                                    const char* key,
                                    const char* value)
{
    auto work = begin(backend);
    const auto revision = work.transaction()->create_if_absent("sessions", key, value);
    CHECK(revision);
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    return revision.value();
}

void test_schema_and_revision_boundaries(const BackendFixture& fixture)
{
    auto backend = fixture.create_backend(stable_schema());
    CHECK(backend->validate_schema(stable_schema()));
    CHECK(!backend->validate_schema(imtg::StorageSchema{2, {{"meta", 1}}}));

    const auto first = imtg::next_storage_revision(0);
    CHECK(first && first.value() == 1);
    const auto last = imtg::next_storage_revision(
        std::numeric_limits<imtg::StorageRevision>::max() - 1);
    CHECK(last && last.value() == std::numeric_limits<imtg::StorageRevision>::max());
    const auto exhausted = imtg::next_storage_revision(
        std::numeric_limits<imtg::StorageRevision>::max());
    CHECK(!exhausted);
    CHECK(exhausted.error().code == imtg::ErrorCode::StorageRevisionExhausted);

    CHECK(create_record(*backend, "zero", "value") == 1);
    auto zero = begin(*backend);
    const auto rejected = zero.transaction()->write("sessions", "zero", "bad", 0);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageConflict);

    for (const auto state : {imtg::MigrationState::Migrating,
                             imtg::MigrationState::RecoveryRequired}) {
        auto schema = stable_schema();
        schema.migration_state = state;
        auto unavailable = fixture.create_backend(schema);
        CHECK(!unavailable->validate_schema(schema));
        const auto intake = unavailable->begin_transaction(imtg::TransactionMode::ReadOnly);
        CHECK(!intake && intake.error().code == imtg::ErrorCode::StorageSchemaFailure);
    }
}

void test_read_your_writes_and_revisions(const BackendFixture& fixture)
{
    auto backend = fixture.create_backend(stable_schema());
    auto create = begin(*backend);
    const auto created = create.transaction()->create_if_absent("sessions", "one", "alpha");
    CHECK(created && created.value() == 1);
    const auto rewritten_create = create.transaction()->write("sessions", "one", "beta", 1);
    CHECK(rewritten_create && rewritten_create.value() == 1);
    const auto pending_create = create.transaction()->read("sessions", "one");
    CHECK(pending_create && pending_create.value()->value == "beta"
          && pending_create.value()->revision == 1);
    CHECK(create.commit().outcome == imtg::CommitOutcome::Committed);

    auto repeated = begin(*backend);
    const auto first_write = repeated.transaction()->write("sessions", "one", "gamma", 1);
    CHECK(first_write && first_write.value() == 2);
    const auto second_write = repeated.transaction()->write("sessions", "one", "delta", 2);
    CHECK(second_write && second_write.value() == 2);
    const auto pending = repeated.transaction()->read("sessions", "one");
    CHECK(pending && pending.value()->value == "delta" && pending.value()->revision == 2);
    CHECK(repeated.commit().outcome == imtg::CommitOutcome::Committed);

    auto follow_up = begin(*backend);
    const auto committed = follow_up.transaction()->read("sessions", "one");
    CHECK(committed && committed.value()->value == "delta"
          && committed.value()->revision == second_write.value());
    const auto next = follow_up.transaction()->write(
        "sessions", "one", "epsilon", second_write.value());
    CHECK(next && next.value() == 3);
    CHECK(follow_up.commit().outcome == imtg::CommitOutcome::Committed);

    auto verify = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto final = verify.transaction()->read("sessions", "one");
    CHECK(final && final.value()->value == "epsilon"
          && final.value()->revision == next.value());
}

void test_snapshot_and_basic_constraints(const BackendFixture& fixture)
{
    auto backend = fixture.create_backend(stable_schema());
    CHECK(create_record(*backend, "snapshot", "old") == 1);

    auto old_snapshot = begin(*backend, imtg::TransactionMode::ReadOnly);
    auto writer = begin(*backend);
    const auto updated = writer.transaction()->write("sessions", "snapshot", "new", 1);
    CHECK(updated && updated.value() == 2);
    CHECK(writer.commit().outcome == imtg::CommitOutcome::Committed);

    const auto old_value = old_snapshot.transaction()->read("sessions", "snapshot");
    CHECK(old_value && old_value.value()->value == "old"
          && old_value.value()->revision == 1);
    auto current = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto current_value = current.transaction()->read("sessions", "snapshot");
    CHECK(current_value && current_value.value()->value == "new"
          && current_value.value()->revision == 2);

    auto duplicate = begin(*backend);
    const auto duplicate_result = duplicate.transaction()->create_if_absent(
        "sessions", "snapshot", "duplicate");
    CHECK(!duplicate_result
          && duplicate_result.error().code == imtg::ErrorCode::StorageConflict);

    auto read_only = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto forbidden = read_only.transaction()->write("sessions", "snapshot", "bad", 2);
    CHECK(!forbidden && forbidden.error().code == imtg::ErrorCode::StorageReadOnly);

    {
        auto automatic_rollback = begin(*backend);
        CHECK(automatic_rollback.transaction()->create_if_absent(
            "sessions", "rollback", "pending"));
    }
    auto rollback_verify = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto absent = rollback_verify.transaction()->read("sessions", "rollback");
    CHECK(absent && !absent.value().has_value());
}

void test_delete_recreate_aba(const BackendFixture& fixture)
{
    auto backend = fixture.create_backend(stable_schema());
    CHECK(create_record(*backend, "aba", "generation-one") == 1);
    auto deleting = begin(*backend);
    CHECK(deleting.transaction()->erase("sessions", "aba", 1));
    CHECK(deleting.commit().outcome == imtg::CommitOutcome::Committed);

    auto recreating = begin(*backend);
    const auto recreated = recreating.transaction()->create_if_absent(
        "sessions", "aba", "generation-three");
    CHECK(recreated && recreated.value() == 3);
    CHECK(recreating.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale_cas = begin(*backend);
    const auto rejected = stale_cas.transaction()->write(
        "sessions", "aba", "stale-overwrite", 1);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageConflict);

    auto verify = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto current = verify.transaction()->read("sessions", "aba");
    CHECK(current && current.value()->value == "generation-three"
          && current.value()->revision == 3);

    if (!fixture.supports_multiple_active_writers) return;

    CHECK(create_record(*backend, "aba-active", "generation-one") == 1);
    auto stale = begin(*backend);
    const auto original = stale.transaction()->read("sessions", "aba-active");
    CHECK(original && original.value()->revision == 1);
    auto active_delete = begin(*backend);
    CHECK(active_delete.transaction()->erase("sessions", "aba-active", 1));
    CHECK(active_delete.commit().outcome == imtg::CommitOutcome::Committed);
    auto active_recreate = begin(*backend);
    const auto active_recreated = active_recreate.transaction()->create_if_absent(
        "sessions", "aba-active", "generation-three");
    CHECK(active_recreated && active_recreated.value() == 3);
    CHECK(active_recreate.commit().outcome == imtg::CommitOutcome::Committed);
    const auto stale_write = stale.transaction()->write(
        "sessions", "aba-active", "stale-overwrite", 1);
    CHECK(stale_write && stale_write.value() == 2);
    CHECK(stale.commit().outcome == imtg::CommitOutcome::RejectedConflict);
}

void test_atomicity_and_conflicts(const BackendFixture& fixture)
{
    {
        auto backend = fixture.create_backend(stable_schema());
        auto multi = begin(*backend);
        CHECK(multi.transaction()->create_if_absent("sessions", "atomic-a", "a"));
        CHECK(multi.transaction()->create_if_absent("sessions", "atomic-b", "b"));
        CHECK(multi.commit().outcome == imtg::CommitOutcome::Committed);
        auto verify = begin(*backend, imtg::TransactionMode::ReadOnly);
        CHECK(verify.transaction()->read("sessions", "atomic-a").value().has_value());
        CHECK(verify.transaction()->read("sessions", "atomic-b").value().has_value());
    }

    if (!fixture.supports_multiple_active_writers) return;

    {
        auto backend = fixture.create_backend(stable_schema());
        CHECK(create_record(*backend, "conflict", "base") == 1);
        auto first = begin(*backend);
        auto second = begin(*backend);
        CHECK(first.transaction()->write("sessions", "conflict", "first", 1));
        CHECK(second.transaction()->write("sessions", "conflict", "second", 1));
        CHECK(first.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(second.commit().outcome == imtg::CommitOutcome::RejectedConflict);
    }

    {
        auto backend = fixture.create_backend(stable_schema());
        auto multi = begin(*backend);
        CHECK(multi.transaction()->create_if_absent("sessions", "conflict-a", "a"));
        CHECK(multi.transaction()->create_if_absent("sessions", "conflict-b", "b"));
        auto competing = begin(*backend);
        CHECK(competing.transaction()->create_if_absent("sessions", "conflict-b", "other"));
        CHECK(competing.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(multi.commit().outcome == imtg::CommitOutcome::RejectedConflict);
        auto verify = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto absent = verify.transaction()->read("sessions", "conflict-a");
        CHECK(absent && !absent.value().has_value());
    }
}

void test_thread_ownership(const BackendFixture& fixture)
{
    auto backend = fixture.create_backend(stable_schema());
    auto owner = begin(*backend);
    std::atomic<bool> rejected{false};
    std::thread other([&owner, &rejected] {
        const auto result = owner.transaction()->read("sessions", "one");
        rejected = !result && result.error().code == imtg::ErrorCode::StoragePermissionDenied;
    });
    other.join();
    CHECK(rejected.load());
}

struct TrackingState {
    int rollback_calls = 0;
    int destruction_calls = 0;
};

class TrackingTransaction final : public imtg::ITransaction {
public:
    TrackingTransaction(TrackingState& tracking, imtg::TransactionState state)
        : m_tracking(tracking), m_state(state)
    {
    }

    ~TrackingTransaction() override { ++m_tracking.destruction_calls; }

    imtg::TransactionMode mode() const noexcept override
    {
        return imtg::TransactionMode::ReadWrite;
    }
    imtg::TransactionState state() const noexcept override { return m_state; }
    imtg::Result<std::optional<imtg::StorageRecord>> read(
        std::string_view, std::string_view) override
    {
        return imtg::Error{imtg::ErrorCode::InvalidState, "tracking transaction"};
    }
    imtg::Result<imtg::StorageRevision> create_if_absent(
        std::string_view, std::string_view, std::string_view) override
    {
        return imtg::Error{imtg::ErrorCode::InvalidState, "tracking transaction"};
    }
    imtg::Result<imtg::StorageRevision> write(
        std::string_view, std::string_view, std::string_view,
        imtg::StorageRevision) override
    {
        return imtg::Error{imtg::ErrorCode::InvalidState, "tracking transaction"};
    }
    imtg::Result<void> erase(
        std::string_view, std::string_view, imtg::StorageRevision) override
    {
        return imtg::Error{imtg::ErrorCode::InvalidState, "tracking transaction"};
    }
    imtg::CommitResult commit() override
    {
        return imtg::CommitResult{imtg::CommitOutcome::Failed,
                                  imtg::Error{imtg::ErrorCode::InvalidState,
                                              "tracking transaction"}};
    }
    void rollback() noexcept override
    {
        ++m_tracking.rollback_calls;
        m_state = imtg::TransactionState::RolledBack;
    }

private:
    TrackingState& m_tracking;
    imtg::TransactionState m_state;
};

imtg::UnitOfWork tracked_work(TrackingState& tracking, imtg::TransactionState state)
{
    return imtg::UnitOfWork(std::make_unique<TrackingTransaction>(tracking, state));
}

void test_unit_of_work_move_assignment()
{
    {
        TrackingState replaced;
        TrackingState incoming;
        {
            auto left = tracked_work(replaced, imtg::TransactionState::Active);
            auto right = tracked_work(incoming, imtg::TransactionState::Active);
            left = std::move(right);
            CHECK(replaced.rollback_calls == 1 && replaced.destruction_calls == 1);
            CHECK(!right.valid() && left.valid());
        }
        CHECK(incoming.rollback_calls == 1 && incoming.destruction_calls == 1);
    }
    {
        TrackingState committed;
        TrackingState incoming;
        {
            auto left = tracked_work(committed, imtg::TransactionState::Committed);
            auto right = tracked_work(incoming, imtg::TransactionState::Unknown);
            left = std::move(right);
            CHECK(committed.rollback_calls == 0 && committed.destruction_calls == 1);
        }
        CHECK(incoming.rollback_calls == 0 && incoming.destruction_calls == 1);
    }
    {
        TrackingState unknown;
        TrackingState incoming;
        {
            auto left = tracked_work(unknown, imtg::TransactionState::Unknown);
            auto right = tracked_work(incoming, imtg::TransactionState::Committed);
            left = std::move(right);
            CHECK(unknown.rollback_calls == 0 && unknown.destruction_calls == 1);
        }
        CHECK(incoming.rollback_calls == 0 && incoming.destruction_calls == 1);
    }
    {
        TrackingState self;
        {
            auto work = tracked_work(self, imtg::TransactionState::Active);
            work = std::move(work);
            CHECK(work.valid() && self.rollback_calls == 0 && self.destruction_calls == 0);
        }
        CHECK(self.rollback_calls == 1 && self.destruction_calls == 1);
    }
    {
        TrackingState committed;
        TrackingState unknown;
        auto committed_work = tracked_work(committed, imtg::TransactionState::Committed);
        auto unknown_work = tracked_work(unknown, imtg::TransactionState::Unknown);
        committed_work.rollback();
        unknown_work.rollback();
        CHECK(committed.rollback_calls == 0 && unknown.rollback_calls == 0);
    }
}

void run_conformance(const BackendFixture& fixture)
{
    test_schema_and_revision_boundaries(fixture);
    test_read_your_writes_and_revisions(fixture);
    test_snapshot_and_basic_constraints(fixture);
    test_delete_recreate_aba(fixture);
    test_atomicity_and_conflicts(fixture);
    test_thread_ownership(fixture);
}

void run_in_memory_failure_tests()
{
    {
        imtg::InMemoryStorageBackend backend(stable_schema());
        CHECK(create_record(backend, "no-op-failed", "original") == 1);
        imtg::InMemoryFailureInjection injection;
        injection.fail_before_publish = true;
        backend.set_failure_injection(injection);

        auto no_op = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto original = no_op.transaction()->read("sessions", "no-op-failed");
        CHECK(original && original.value()->value == "original"
              && original.value()->revision == 1);
        CHECK(no_op.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(no_op.transaction()->state() == imtg::TransactionState::Committed);
        CHECK(backend.record_count() == 1);

        auto failed = begin(backend);
        CHECK(failed.transaction()->write("sessions", "no-op-failed", "changed", 1));
        CHECK(failed.commit().outcome == imtg::CommitOutcome::Failed);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto unchanged = verify.transaction()->read("sessions", "no-op-failed");
        CHECK(unchanged && unchanged.value()->value == "original"
              && unchanged.value()->revision == 1);
    }
    {
        imtg::InMemoryStorageBackend backend(stable_schema());
        CHECK(create_record(backend, "no-op-unknown-before", "original") == 1);
        imtg::InMemoryFailureInjection injection;
        injection.unknown_before_publish = true;
        backend.set_failure_injection(injection);

        auto no_op = begin(backend);
        CHECK(no_op.commit().outcome == imtg::CommitOutcome::Committed);
        auto unknown = begin(backend);
        CHECK(unknown.transaction()->write(
            "sessions", "no-op-unknown-before", "changed", 1));
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto unchanged = verify.transaction()->read(
            "sessions", "no-op-unknown-before");
        CHECK(unchanged && unchanged.value()->value == "original"
              && unchanged.value()->revision == 1);
    }
    {
        imtg::InMemoryStorageBackend backend(stable_schema());
        CHECK(create_record(backend, "no-op-unknown-after", "original") == 1);
        imtg::InMemoryFailureInjection injection;
        injection.unknown_after_publish = true;
        backend.set_failure_injection(injection);

        auto no_op = begin(backend);
        CHECK(no_op.commit().outcome == imtg::CommitOutcome::Committed);
        auto unknown = begin(backend);
        CHECK(unknown.transaction()->write(
            "sessions", "no-op-unknown-after", "changed", 1));
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto published = verify.transaction()->read(
            "sessions", "no-op-unknown-after");
        CHECK(published && published.value()->value == "changed"
              && published.value()->revision == 2);
    }
    imtg::InMemoryStorageBackend backend(stable_schema());
    {
        imtg::InMemoryFailureInjection injection;
        injection.unknown_before_publish = true;
        backend.set_failure_injection(injection);
        auto unknown = begin(backend);
        CHECK(unknown.transaction()->create_if_absent("sessions", "unknown-before", "x"));
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        CHECK(unknown.transaction()->state() == imtg::TransactionState::Unknown);
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        unknown.rollback();
        CHECK(unknown.transaction()->state() == imtg::TransactionState::Unknown);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto absent = verify.transaction()->read("sessions", "unknown-before");
        CHECK(absent && !absent.value().has_value());
    }
    {
        imtg::InMemoryFailureInjection injection;
        injection.unknown_after_publish = true;
        backend.set_failure_injection(injection);
        auto unknown = begin(backend);
        CHECK(unknown.transaction()->create_if_absent("sessions", "unknown-after", "x"));
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        unknown.rollback();
        CHECK(unknown.transaction()->state() == imtg::TransactionState::Unknown);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto present = verify.transaction()->read("sessions", "unknown-after");
        CHECK(present && present.value().has_value());
    }
    {
        imtg::InMemoryFailureInjection injection;
        injection.fail_write_set_preparation = true;
        backend.set_failure_injection(injection);
        auto failed = begin(backend);
        const auto result = failed.transaction()->create_if_absent(
            "sessions", "prepare-failure", "x");
        CHECK(!result && result.error().code == imtg::ErrorCode::StorageUnavailable);
    }
    {
        imtg::InMemoryFailureInjection injection;
        injection.fail_before_publish = true;
        backend.set_failure_injection(injection);
        auto failed = begin(backend);
        CHECK(failed.transaction()->create_if_absent("sessions", "commit-failure", "x"));
        CHECK(failed.commit().outcome == imtg::CommitOutcome::Failed);
        CHECK(failed.transaction()->state() == imtg::TransactionState::RolledBack);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto absent = verify.transaction()->read("sessions", "commit-failure");
        CHECK(absent && !absent.value().has_value());
    }
}

} // namespace

int main()
{
    const BackendFixture in_memory{
        [](imtg::StorageSchema schema) {
            return std::make_unique<imtg::InMemoryStorageBackend>(std::move(schema));
        },
        true,
    };
    run_conformance(in_memory);
    run_in_memory_failure_tests();
    test_unit_of_work_move_assignment();
    return 0;
}
