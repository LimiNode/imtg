#include "imtg/storage/storage.hpp"

#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

imtg::UnitOfWork begin(imtg::IStorageBackend& backend,
                       imtg::TransactionMode mode = imtg::TransactionMode::ReadWrite)
{
    auto result = imtg::UnitOfWork::begin(backend, mode);
    CHECK(result);
    return std::move(result).value();
}

void run_conformance(imtg::IStorageBackend& backend)
{
    auto schema = backend.schema();
    CHECK(schema);
    CHECK(backend.validate_schema(schema.value()));

    {
        auto work = begin(backend);
        auto* transaction = work.transaction();
        CHECK(transaction != nullptr);
        const auto missing = transaction->read("sessions", "one");
        CHECK(missing && !missing.value().has_value());
        CHECK(transaction->create_if_absent("sessions", "one", "alpha"));
        const auto own_write = transaction->read("sessions", "one");
        CHECK(own_write && own_write.value()->value == "alpha"
              && own_write.value()->revision == 1);
        CHECK(transaction->write("sessions", "one", "beta", 1));
        const auto committed = work.commit();
        CHECK(committed.outcome == imtg::CommitOutcome::Committed);
        CHECK(transaction->state() == imtg::TransactionState::Committed);
        const auto repeated = transaction->commit();
        CHECK(repeated.outcome == imtg::CommitOutcome::Failed
              && repeated.error.has_value()
              && repeated.error->code == imtg::ErrorCode::InvalidState);
    }

    {
        auto repeated = begin(backend);
        CHECK(repeated.transaction()->write("sessions", "one", "gamma", 1));
        CHECK(repeated.transaction()->write("sessions", "one", "delta", 2));
        const auto visible = repeated.transaction()->read("sessions", "one");
        CHECK(visible && visible.value()->value == "delta" && visible.value()->revision == 3);
        CHECK(repeated.commit().outcome == imtg::CommitOutcome::Committed);
    }

    {
        auto read_only = begin(backend, imtg::TransactionMode::ReadOnly);
        CHECK(!read_only.transaction()->write("sessions", "one", "no", 2));
        CHECK(read_only.transaction()->write("sessions", "one", "no", 2).error().code
              == imtg::ErrorCode::StorageReadOnly);
        read_only.rollback();
    }

    {
        auto rollback = begin(backend);
        CHECK(rollback.transaction()->create_if_absent("sessions", "rollback", "x"));
        rollback.rollback();
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto absent = verify.transaction()->read("sessions", "rollback");
        CHECK(absent && !absent.value().has_value());
    }

    {
        auto recreate = begin(backend);
        CHECK(recreate.transaction()->erase("sessions", "one", 2));
        const auto recreated = recreate.transaction()->create_if_absent(
            "sessions", "one", "recreated");
        CHECK(recreated && recreated.value() == 3);
        const auto visible = recreate.transaction()->read("sessions", "one");
        CHECK(visible && visible.value()->value == "recreated"
              && visible.value()->revision == 3);
        CHECK(recreate.commit().outcome == imtg::CommitOutcome::Committed);
    }

    {
        auto duplicate = begin(backend);
        CHECK(!duplicate.transaction()->create_if_absent("sessions", "one", "duplicate"));
        CHECK(duplicate.transaction()->create_if_absent("sessions", "one", "duplicate").error().code
              == imtg::ErrorCode::StorageConflict);
        duplicate.rollback();
    }

    {
        auto first = begin(backend);
        auto second = begin(backend);
        CHECK(first.transaction()->write("sessions", "one", "first", 3));
        CHECK(second.transaction()->write("sessions", "one", "second", 3));
        CHECK(first.commit().outcome == imtg::CommitOutcome::Committed);
        const auto conflict = second.commit();
        CHECK(conflict.outcome == imtg::CommitOutcome::RejectedConflict);
        CHECK(second.transaction()->state() == imtg::TransactionState::RolledBack);
    }

    {
        auto multi = begin(backend);
        CHECK(multi.transaction()->create_if_absent("sessions", "atomic-a", "a"));
        CHECK(multi.transaction()->create_if_absent("sessions", "atomic-b", "b"));
        auto competing = begin(backend);
        CHECK(competing.transaction()->create_if_absent("sessions", "atomic-b", "other"));
        CHECK(competing.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(multi.commit().outcome == imtg::CommitOutcome::RejectedConflict);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto first_absent = verify.transaction()->read("sessions", "atomic-a");
        CHECK(first_absent && !first_absent.value().has_value());
    }

    {
        auto owner = begin(backend);
        std::atomic<bool> rejected{false};
        std::thread other([&owner, &rejected] {
            const auto result = owner.transaction()->read("sessions", "one");
            rejected = !result && result.error().code == imtg::ErrorCode::StoragePermissionDenied;
        });
        other.join();
        CHECK(rejected.load());
    }
}

void run_in_memory_failure_tests(imtg::InMemoryStorageBackend& backend)
{
    {
        imtg::InMemoryFailureInjection injection;
        injection.unknown_before_publish = true;
        backend.set_failure_injection(injection);
        auto unknown = begin(backend);
        CHECK(unknown.transaction()->create_if_absent("sessions", "unknown-before", "x"));
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
        CHECK(unknown.transaction()->state() == imtg::TransactionState::Unknown);
        CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);
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
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto present = verify.transaction()->read("sessions", "unknown-after");
        CHECK(present && present.value().has_value());
    }

    {
        imtg::InMemoryFailureInjection injection;
        injection.fail_write_set_preparation = true;
        backend.set_failure_injection(injection);
        auto failed = begin(backend);
        const auto preparation = failed.transaction()->create_if_absent(
            "sessions", "prep-failure", "x");
        CHECK(!preparation);
        CHECK(preparation.error().code == imtg::ErrorCode::StorageUnavailable);
        failed.rollback();
    }

    {
        imtg::InMemoryFailureInjection injection;
        injection.fail_before_publish = true;
        backend.set_failure_injection(injection);
        auto failed = begin(backend);
        CHECK(failed.transaction()->create_if_absent("sessions", "before-failure", "x"));
        const auto result = failed.commit();
        CHECK(result.outcome == imtg::CommitOutcome::Failed);
        CHECK(failed.transaction()->state() == imtg::TransactionState::RolledBack);
        auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
        const auto absent = verify.transaction()->read("sessions", "before-failure");
        CHECK(absent && !absent.value().has_value());
    }
}

} // namespace

int main()
{
    imtg::InMemoryStorageBackend backend(
        imtg::StorageSchema{1, {{"meta", 1}, {"sessions", 1}}});
    run_conformance(backend);
    run_in_memory_failure_tests(backend);
    CHECK(backend.record_count() >= 3);

    imtg::InMemoryStorageBackend incompatible(
        imtg::StorageSchema{2, {{"meta", 1}}});
    CHECK(!incompatible.validate_schema(imtg::StorageSchema{1, {{"meta", 1}}}));
    CHECK(incompatible.validate_schema(imtg::StorageSchema{1, {{"meta", 1}}}).error().code
          == imtg::ErrorCode::StorageSchemaFailure);
    return 0;
}
