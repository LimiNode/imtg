#include "imtg/reconciliation/outbox.hpp"
#include "test_support.hpp"

#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

using namespace imtg;
using namespace std::chrono_literals;

UnitOfWork begin(IStorageBackend& backend, TransactionMode mode = TransactionMode::ReadWrite)
{
    auto result = UnitOfWork::begin(backend, mode);
    CHECK(result);
    return std::move(result).value();
}

ConversationKey conversation()
{
    return ConversationKey{BotId{7}, ChatId{42}, Chat{}};
}

std::string target()
{
    const auto encoded = make_logical_target_identity(conversation(), "body");
    CHECK(encoded);
    return encoded.value();
}

OutboxEffect make_effect(std::string id, StorageRevision desired, std::string payload = "payload")
{
    const RenderIntentId render_intent{std::move(id)};
    const auto target_identity = target();
    ReconciliationInput input{
        render_intent, target_identity, EffectKind::Edit, "body", desired, 0,
        RenderPolicy::OnChange, 1, std::move(payload)};
    input.conversation = conversation();
    input.session_revision = desired;
    input.desired_generation = 1;
    input.desired_generation_row_revision = 1;
    input.applied_target_revision = 0;
    input.context = ReconciliationContext{
        render_intent, conversation(), desired, desired, 1, 1, target_identity,
        EffectKind::Edit, "body", 0, 0, RenderPolicy::OnChange,
        ClaimToken{1}, 1};
    ReconciliationPlanner planner;
    const auto plan = planner.build(input);
    CHECK(plan && !plan.value().no_op);
    return plan.value().effect;
}

void insert_effect(InMemoryStorageBackend& backend, const OutboxEffect& effect)
{
    OutboxRepository repository;
    auto work = begin(backend);
    CHECK(repository.create_or_validate(effect, *work.transaction()));
    CHECK(work.commit().outcome == CommitOutcome::Committed);
}

OutboxEffect read_effect(InMemoryStorageBackend& backend, const OutboxEffectId& id)
{
    OutboxRepository repository;
    auto work = begin(backend, TransactionMode::ReadOnly);
    const auto effect = repository.load(id, *work.transaction());
    CHECK(effect && effect.value());
    CHECK(work.commit().outcome == CommitOutcome::Committed);
    return *effect.value();
}

void test_competing_claims_and_lease_fencing()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    const auto effect = make_effect("claim-race", 1);
    insert_effect(backend, effect);

    auto left = begin(backend);
    auto right = begin(backend);
    const auto left_page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {},
                                                            *left.transaction());
    const auto right_page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {},
                                                             *right.transaction());
    CHECK(left_page && right_page && left_page.value().effects.size() == 1
          && right_page.value().effects.size() == 1);
    const auto left_claim = left_page.value().effects.front();
    const auto right_claim = right_page.value().effects.front();
    CHECK(left_claim.claim_token.value == 1 && right_claim.claim_token.value == 1);
    CHECK(left.commit().outcome == CommitOutcome::Committed);
    CHECK(right.commit().outcome == CommitOutcome::RejectedConflict);

    auto renew = begin(backend);
    const auto renewed = repository.renew_outbox_effect(
        effect.id, left_claim.claim_token, left_claim.revision, ClockTime{},
        ClockTime{2h}, *renew.transaction());
    CHECK(renewed && renew.commit().outcome == CommitOutcome::Committed);

    auto expired = begin(backend);
    const auto stale = repository.renew_outbox_effect(
        effect.id, left_claim.claim_token, renewed.value().revision,
        ClockTime{3h}, ClockTime{4h}, *expired.transaction());
    CHECK(!stale && stale.error().code == ErrorCode::StorageStaleClaim);
    CHECK(expired.transaction()->state() == TransactionState::RolledBack);

    auto reclaim = begin(backend);
    const auto reclaimed = repository.claim_outbox_effects(
        1, ClockTime{3h}, 1h, {}, *reclaim.transaction());
    CHECK(reclaimed && reclaimed.value().effects.size() == 1
          && reclaimed.value().effects.front().claim_token.value == 2);
    CHECK(reclaim.commit().outcome == CommitOutcome::Committed);

    auto stale_ack = begin(backend);
    const auto old_admission = repository.mark_dispatch_admitted(
        effect.id, left_claim.claim_token, renewed.value().revision,
        ClockTime{3h}, *stale_ack.transaction());
    CHECK(!old_admission && old_admission.error().code == ErrorCode::StorageStaleClaim);
    CHECK(stale_ack.transaction()->state() == TransactionState::RolledBack);
}

void test_dispatch_boundary_and_unknown_recovery()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    const auto effect = make_effect("dispatch-boundary", 1);
    insert_effect(backend, effect);

    auto claim_work = begin(backend);
    const auto claimed = repository.claim_outbox_effects(
        1, ClockTime{}, 1h, {}, *claim_work.transaction());
    CHECK(claimed && claimed.value().effects.size() == 1);
    const auto claim = claimed.value().effects.front();
    CHECK(claim_work.commit().outcome == CommitOutcome::Committed);

    auto admission_work = begin(backend);
    const auto admitted = repository.mark_dispatch_admitted(
        effect.id, claim.claim_token, claim.revision, ClockTime{},
        *admission_work.transaction());
    CHECK(admitted && admitted.value().state == OutboxEffectState::DispatchAdmitted);
    CHECK(admission_work.commit().outcome == CommitOutcome::Committed);

    // A worker crash after the durable marker must not make the effect claimable.
    auto recovery_work = begin(backend);
    const auto recovery = repository.claim_outbox_effects(
        1, ClockTime{2h}, 1h, {}, *recovery_work.transaction());
    CHECK(recovery && recovery.value().effects.empty());
    CHECK(recovery_work.commit().outcome == CommitOutcome::Committed);
    const auto unknown = read_effect(backend, effect.id);
    CHECK(unknown.state == OutboxEffectState::UnknownOutcome);

    auto blind_retry = begin(backend);
    const auto rejected = repository.retry_unknown_effect(
        effect.id, unknown.claim_token, unknown.revision, ClockTime{2h}, false,
        ClockTime{2h}, *blind_retry.transaction());
    CHECK(!rejected && rejected.error().code == ErrorCode::InvalidState);
    CHECK(blind_retry.transaction()->state() == TransactionState::RolledBack);

    auto safe_retry = begin(backend);
    const auto requeued = repository.retry_unknown_effect(
        effect.id, unknown.claim_token, unknown.revision, ClockTime{2h}, true,
        ClockTime{3h}, *safe_retry.transaction());
    CHECK(requeued && requeued.value().state == OutboxEffectState::RetryableFailure);
    CHECK(safe_retry.commit().outcome == CommitOutcome::Committed);

    auto retry_claim = begin(backend);
    const auto not_yet = repository.claim_outbox_effects(
        1, ClockTime{2h}, 1h, {}, *retry_claim.transaction());
    CHECK(not_yet && not_yet.value().effects.empty());
    CHECK(retry_claim.commit().outcome == CommitOutcome::Committed);
    auto eligible_claim = begin(backend);
    const auto eligible = repository.claim_outbox_effects(
        1, ClockTime{3h}, 1h, {}, *eligible_claim.transaction());
    CHECK(eligible && eligible.value().effects.size() == 1);
    const auto eligible_effect = eligible.value().effects.front();
    CHECK(eligible_claim.commit().outcome == CommitOutcome::Committed);

    auto rejected_work = begin(backend);
    const auto retryable = repository.record_retryable_failure(
        effect.id, eligible_effect.claim_token, eligible_effect.revision,
        ClockTime{3h}, ClockTime{4h}, *rejected_work.transaction());
    CHECK(retryable && retryable.value().state == OutboxEffectState::RetryableFailure);
    CHECK(rejected_work.commit().outcome == CommitOutcome::Committed);

    // A lost commit acknowledgement is resolved by reading the authoritative row.
    auto ambiguous_claim_work = begin(backend);
    const auto ambiguous_claim = repository.claim_outbox_effects(
        1, ClockTime{4h}, 1h, {}, *ambiguous_claim_work.transaction());
    CHECK(ambiguous_claim && ambiguous_claim.value().effects.size() == 1);
    const auto ambiguous_effect = ambiguous_claim.value().effects.front();
    CHECK(ambiguous_claim_work.commit().outcome == CommitOutcome::Committed);
    auto ambiguous_transition = begin(backend);
    const auto admitted_again = repository.mark_dispatch_admitted(
        effect.id, ambiguous_effect.claim_token, ambiguous_effect.revision,
        ClockTime{4h}, *ambiguous_transition.transaction());
    CHECK(admitted_again);
    InMemoryFailureInjection injection;
    injection.unknown_after_publish = true;
    backend.set_failure_injection(injection);
    const auto unknown_commit = ambiguous_transition.commit();
    CHECK(unknown_commit.outcome == CommitOutcome::Unknown);
    const auto authoritative = read_effect(backend, effect.id);
    CHECK(authoritative.state == OutboxEffectState::DispatchAdmitted);
}

void test_acknowledgement_is_atomic_and_monotonic()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    AppliedTargetRepository applied;
    const auto newer = make_effect("ack-newer", 3, "newer");
    insert_effect(backend, newer);

    auto claim_work = begin(backend);
    const auto claimed = repository.claim_outbox_effects(
        1, ClockTime{}, 1h, {}, *claim_work.transaction());
    CHECK(claimed && claimed.value().effects.size() == 1);
    const auto claim = claimed.value().effects.front();
    CHECK(claim_work.commit().outcome == CommitOutcome::Committed);
    auto admission_work = begin(backend);
    const auto admitted = repository.mark_dispatch_admitted(
        newer.id, claim.claim_token, claim.revision, ClockTime{},
        *admission_work.transaction());
    CHECK(admitted && admission_work.commit().outcome == CommitOutcome::Committed);

    auto acknowledge_work = begin(backend);
    const auto succeeded = repository.acknowledge_success(
        newer.id, claim.claim_token, admitted.value().revision, ClockTime{},
        std::string{"telegram:message:9"}, applied, *acknowledge_work.transaction());
    CHECK(succeeded && succeeded.value().state == OutboxEffectState::Succeeded);
    CHECK(acknowledge_work.commit().outcome == CommitOutcome::Committed);

    auto target_work = begin(backend, TransactionMode::ReadOnly);
    const auto target_state = applied.load(target(), *target_work.transaction());
    CHECK(target_state && target_state.value()
          && target_state.value()->desired_revision == 3
          && target_state.value()->confirmed_applied_revision == 3
          && target_state.value()->physical_identity
          && *target_state.value()->physical_identity == "telegram:message:9");
    CHECK(target_work.commit().outcome == CommitOutcome::Committed);

    // Duplicate completion is idempotent for the same fenced row.
    auto duplicate_work = begin(backend);
    const auto duplicate = repository.acknowledge_success(
        newer.id, claim.claim_token, succeeded.value().revision, ClockTime{},
        std::nullopt, applied, *duplicate_work.transaction());
    CHECK(duplicate && duplicate.value().state == OutboxEffectState::Succeeded);
    CHECK(duplicate_work.commit().outcome == CommitOutcome::Committed);

    const auto older = make_effect("ack-older", 2, "older");
    insert_effect(backend, older);
    auto old_claim_work = begin(backend);
    const auto old_claim_page = repository.claim_outbox_effects(
        1, ClockTime{}, 1h, {}, *old_claim_work.transaction());
    CHECK(old_claim_page && old_claim_page.value().effects.empty());
    CHECK(old_claim_work.commit().outcome == CommitOutcome::Committed);
    CHECK(read_effect(backend, older.id).state == OutboxEffectState::Failed);

    auto deletion = make_effect("delete-confirmed-target", 4);
    deletion.slot.effect_kind = EffectKind::Delete;
    deletion.id = OutboxEffectId::from_slot(deletion.slot);
    insert_effect(backend, deletion);
    auto delete_claim_work = begin(backend);
    const auto delete_page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {},
                                                            *delete_claim_work.transaction());
    CHECK(delete_page && delete_page.value().effects.size() == 1);
    const auto delete_claim = delete_page.value().effects.front();
    CHECK(delete_claim_work.commit().outcome == CommitOutcome::Committed);
    auto delete_marker_work = begin(backend);
    const auto delete_marker = repository.mark_dispatch_admitted(deletion.id, delete_claim.claim_token,
        delete_claim.revision, ClockTime{}, *delete_marker_work.transaction());
    CHECK(delete_marker && delete_marker_work.commit().outcome == CommitOutcome::Committed);
    auto delete_ack = begin(backend);
    CHECK(repository.acknowledge_success(deletion.id, delete_marker.value().claim_token,
        delete_marker.value().revision, ClockTime{}, std::nullopt, applied, *delete_ack.transaction()));
    CHECK(delete_ack.commit().outcome == CommitOutcome::Committed);
    auto deleted_target = begin(backend, TransactionMode::ReadOnly);
    const auto deleted = applied.load(target(), *deleted_target.transaction());
    CHECK(deleted && deleted.value() && !deleted.value()->physical_identity
          && deleted.value()->confirmed_applied_revision == 4);
}

OutboxEffect claim_one(InMemoryStorageBackend& backend, OutboxRepository& repository,
                       ClockTime now = ClockTime{})
{
    auto work = begin(backend);
    const auto page = repository.claim_outbox_effects(1, now, 1h, {}, *work.transaction());
    CHECK(page && page.value().effects.size() == 1);
    CHECK(work.commit().outcome == CommitOutcome::Committed);
    return page.value().effects.front();
}

OutboxEffect admit(InMemoryStorageBackend& backend, OutboxRepository& repository,
                   const OutboxEffect& claim, ClockTime now = ClockTime{})
{
    auto work = begin(backend);
    const auto admitted = repository.mark_dispatch_admitted(
        claim.id, claim.claim_token, claim.revision, now, *work.transaction());
    CHECK(admitted && work.commit().outcome == CommitOutcome::Committed);
    return admitted.value();
}

void test_target_ownership_and_late_completion()
{
    // Invariants 5/8: different effect IDs do not bypass target-level exclusion.
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    AppliedTargetRepository applied;
    const auto older = make_effect("a-target-old", 1);
    const auto newer = make_effect("b-target-new", 2);
    insert_effect(backend, older);
    insert_effect(backend, newer);
    const auto old_claim = claim_one(backend, repository);
    const auto new_claim = claim_one(backend, repository);
    CHECK(old_claim.id == older.id && new_claim.id == newer.id);

    auto first = begin(backend);
    auto second = begin(backend);
    const auto first_marker = repository.mark_dispatch_admitted(
        older.id, old_claim.claim_token, old_claim.revision, ClockTime{}, *first.transaction());
    const auto second_marker = repository.mark_dispatch_admitted(
        newer.id, new_claim.claim_token, new_claim.revision, ClockTime{}, *second.transaction());
    CHECK(first_marker && second_marker);
    CHECK(first.commit().outcome == CommitOutcome::Committed);
    CHECK(second.commit().outcome == CommitOutcome::RejectedConflict);
    auto blocked = begin(backend);
    CHECK(!repository.mark_dispatch_admitted(newer.id, new_claim.claim_token,
        new_claim.revision, ClockTime{}, *blocked.transaction()));
    CHECK(blocked.transaction()->state() == TransactionState::RolledBack);

    auto recovery = begin(backend);
    const auto recovered = repository.claim_outbox_effects(1, ClockTime{2h}, 1h, {},
                                                           *recovery.transaction());
    CHECK(recovered && recovered.value().effects.size() == 1);
    const auto reclaimed_new = recovered.value().effects.front();
    CHECK(recovery.commit().outcome == CommitOutcome::Committed);
    const auto unknown_old = read_effect(backend, older.id);
    CHECK(unknown_old.state == OutboxEffectState::UnknownOutcome);
    auto still_blocked = begin(backend);
    CHECK(!repository.mark_dispatch_admitted(newer.id, reclaimed_new.claim_token,
        reclaimed_new.revision, ClockTime{2h}, *still_blocked.transaction()));

    auto resolution = begin(backend);
    const auto superseded = repository.supersede_effect(
        older.id, unknown_old.claim_token, unknown_old.revision, ClockTime{2h},
        newer.id, *resolution.transaction());
    CHECK(superseded && resolution.commit().outcome == CommitOutcome::Committed);
    const auto admitted_new = admit(backend, repository, reclaimed_new, ClockTime{2h});
    auto completion = begin(backend);
    const auto succeeded = repository.acknowledge_success(
        newer.id, admitted_new.claim_token, admitted_new.revision, ClockTime{2h},
        std::string{"message:new"}, applied, *completion.transaction());
    CHECK(succeeded && completion.commit().outcome == CommitOutcome::Committed);
    auto late = begin(backend);
    const auto stale = repository.acknowledge_success(
        older.id, first_marker.value().claim_token, first_marker.value().revision,
        ClockTime{2h}, std::string{"message:old"}, applied, *late.transaction());
    CHECK(!stale && stale.error().code == ErrorCode::StorageStaleClaim);
    auto verify = begin(backend, TransactionMode::ReadOnly);
    const auto state = applied.load(target(), *verify.transaction());
    CHECK(state && state.value() && state.value()->confirmed_applied_revision == 2
          && state.value()->physical_identity == std::optional<std::string>{"message:new"});
}

// Storage errors at individual writes must invalidate the borrowed transaction.
class FailingWriteTransaction final : public ITransaction {
public:
    FailingWriteTransaction(ITransaction& delegate, unsigned fail_at)
        : m_delegate(delegate), m_fail_at(fail_at) {}
    TransactionMode mode() const noexcept override { return m_delegate.mode(); }
    TransactionState state() const noexcept override { return m_delegate.state(); }
    Result<std::optional<StorageRecord>> read(std::string_view ns, std::string_view key) override
    { return m_delegate.read(ns, key); }
    Result<std::vector<StorageScanEntry>> scan(std::string_view ns, std::string_view cursor,
                                              std::size_t limit) override
    { return m_delegate.scan(ns, cursor, limit); }
    Result<void> watch(std::string_view ns, std::string_view key, StorageRevision revision) override
    { return m_delegate.watch(ns, key, revision); }
    Result<StorageRevision> create_if_absent(std::string_view ns, std::string_view key,
                                            std::string_view value) override
    {
        if (++m_writes == m_fail_at) return Error{ErrorCode::StorageUnavailable, "injected write failure"};
        return m_delegate.create_if_absent(ns, key, value);
    }
    Result<StorageRevision> write(std::string_view ns, std::string_view key,
                                 std::string_view value, StorageRevision revision) override
    {
        if (++m_writes == m_fail_at) return Error{ErrorCode::StorageUnavailable, "injected write failure"};
        return m_delegate.write(ns, key, value, revision);
    }
    Result<void> erase(std::string_view ns, std::string_view key, StorageRevision revision) override
    { return m_delegate.erase(ns, key, revision); }
    CommitResult commit() override { return m_delegate.commit(); }
    void rollback() noexcept override { m_delegate.rollback(); }
private:
    ITransaction& m_delegate;
    unsigned m_fail_at;
    unsigned m_writes{};
};

void test_partial_staging_is_fail_closed()
{
    for (unsigned fail_at = 1; fail_at <= 3; ++fail_at) {
        InMemoryStorageBackend backend;
        OutboxRepository repository;
        AppliedTargetRepository applied;
        const auto effect = make_effect("partial-ack", 1);
        insert_effect(backend, effect);
        const auto claim = claim_one(backend, repository);
        // First reserve the target, then fail effect staging. No lock may leak.
        auto bad_marker = begin(backend);
        FailingWriteTransaction marker_tx(*bad_marker.transaction(), 2);
        CHECK(!repository.mark_dispatch_admitted(effect.id, claim.claim_token,
            claim.revision, ClockTime{}, marker_tx));
        CHECK(bad_marker.transaction()->state() == TransactionState::RolledBack);
        CHECK(bad_marker.commit().outcome == CommitOutcome::Failed);
        CHECK(read_effect(backend, effect.id).state == OutboxEffectState::ClaimedBeforeDispatch);
        const auto admitted = admit(backend, repository, claim);

        auto work = begin(backend);
        FailingWriteTransaction transaction(*work.transaction(), fail_at);
        CHECK(!repository.acknowledge_success(effect.id, admitted.claim_token,
            admitted.revision, ClockTime{}, std::string{"message:1"}, applied, transaction));
        CHECK(work.transaction()->state() == TransactionState::RolledBack);
        CHECK(work.commit().outcome == CommitOutcome::Failed);
        CHECK(read_effect(backend, effect.id).state == OutboxEffectState::DispatchAdmitted);
        auto verify = begin(backend, TransactionMode::ReadOnly);
        const auto state = applied.load(target(), *verify.transaction());
        CHECK(state && !state.value());
    }
}

InMemoryFailureInjection failure_at(unsigned point)
{
    InMemoryFailureInjection injection;
    injection.fail_before_publish = point == 0;
    injection.fail_write_set_preparation = point == 1;
    injection.unknown_before_publish = point == 2;
    injection.unknown_after_publish = point == 3;
    return injection;
}

void test_commit_failure_matrix_and_dispatch_gate()
{
    // Invariants 5/8: no storage result except confirmed Committed admits I/O.
    for (unsigned phase = 0; phase != 3; ++phase) {
        for (unsigned point = 0; point != 4; ++point) {
            InMemoryStorageBackend backend;
            OutboxRepository repository;
            AppliedTargetRepository applied;
            const auto effect = make_effect("commit-matrix", 1);
            insert_effect(backend, effect);
            OutboxEffect previous = read_effect(backend, effect.id);
            if (phase > 0) previous = claim_one(backend, repository);
            if (phase > 1) previous = admit(backend, repository, previous);
            auto work = begin(backend);
            if (point == 1) backend.set_failure_injection(failure_at(point));
            bool staged = false;
            if (phase == 0) {
                const auto page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {},
                                                                  *work.transaction());
                staged = page && page.value().effects.size() == 1;
            } else if (phase == 1) {
                staged = static_cast<bool>(repository.mark_dispatch_admitted(effect.id, previous.claim_token,
                    previous.revision, ClockTime{}, *work.transaction()));
            } else {
                staged = static_cast<bool>(repository.acknowledge_success(effect.id, previous.claim_token,
                    previous.revision, ClockTime{}, std::string{"message:matrix"},
                    applied, *work.transaction()));
            }
            CHECK(staged == (point != 1));
            if (point == 1) CHECK(work.transaction()->state() == TransactionState::RolledBack);
            else backend.set_failure_injection(failure_at(point));
            const auto committed = work.commit();
            CHECK(committed.outcome == (point < 2 ? CommitOutcome::Failed : CommitOutcome::Unknown));
            unsigned fake_transport_calls = 0;
            if (phase == 1 && committed.outcome == CommitOutcome::Committed) ++fake_transport_calls;
            CHECK(fake_transport_calls == 0);
            const auto stored = read_effect(backend, effect.id);
            const auto published_state = phase == 0 ? OutboxEffectState::ClaimedBeforeDispatch
                : phase == 1 ? OutboxEffectState::DispatchAdmitted : OutboxEffectState::Succeeded;
            CHECK(stored.state == (point == 3 ? published_state : previous.state));
            auto verify = begin(backend, TransactionMode::ReadOnly);
            const auto target_state = applied.load(target(), *verify.transaction());
            CHECK(target_state);
            CHECK(target_state.value().has_value() == (phase == 2 && point == 3));
            if (target_state.value()) {
                CHECK(target_state.value()->confirmed_applied_revision == 1
                      && target_state.value()->last_confirmed_effect == std::optional<OutboxEffectId>{effect.id});
            }
            // Even a visible marker after Unknown is not permission to send.
            if (phase == 1 && point == 3) {
                auto recovery = begin(backend);
                const auto page = repository.claim_outbox_effects(1, ClockTime{2h}, 1h, {},
                                                                  *recovery.transaction());
                CHECK(page && page.value().effects.empty());
                CHECK(recovery.commit().outcome == CommitOutcome::Committed);
                CHECK(read_effect(backend, effect.id).state == OutboxEffectState::UnknownOutcome);
            }
        }
    }
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    insert_effect(backend, make_effect("confirmed-gate", 1));
    const auto claim = claim_one(backend, repository);
    auto marker = begin(backend);
    CHECK(repository.mark_dispatch_admitted(claim.id, claim.claim_token, claim.revision,
                                            ClockTime{}, *marker.transaction()));
    unsigned fake_transport_calls = 0;
    if (marker.commit().outcome == CommitOutcome::Committed) ++fake_transport_calls;
    CHECK(fake_transport_calls == 1);
    auto lost_response = begin(backend);
    const auto admitted = read_effect(backend, claim.id);
    CHECK(repository.record_unknown_outcome(admitted.id, admitted.claim_token, admitted.revision,
                                            ClockTime{}, *lost_response.transaction()));
    CHECK(lost_response.commit().outcome == CommitOutcome::Committed);
    auto rescan = begin(backend);
    const auto page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {}, *rescan.transaction());
    CHECK(page && page.value().effects.empty() && fake_transport_calls == 1);
}

void test_target_revision_watch_and_scope_isolation()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    AppliedTargetRepository applied;
    insert_effect(backend, make_effect("target-watch", 1));
    const auto claim = claim_one(backend, repository);
    auto marker = begin(backend);
    CHECK(repository.mark_dispatch_admitted(claim.id, claim.claim_token, claim.revision,
                                            ClockTime{}, *marker.transaction()));
    auto newer_target = begin(backend);
    CHECK(applied.save(AppliedTargetState{target(), {}, 2, 0, {}, 0}, 0,
                        *newer_target.transaction()));
    CHECK(newer_target.commit().outcome == CommitOutcome::Committed);
    CHECK(marker.commit().outcome == CommitOutcome::RejectedConflict);
    auto stale = begin(backend);
    const auto rejected = repository.mark_dispatch_admitted(claim.id, claim.claim_token,
        claim.revision, ClockTime{}, *stale.transaction());
    CHECK(!rejected && rejected.error().code == ErrorCode::StorageStaleClaim);

    InMemoryStorageBackend late_backend;
    insert_effect(late_backend, make_effect("late-desired-revision", 1));
    const auto late_claim = claim_one(late_backend, repository);
    const auto late_marker = admit(late_backend, repository, late_claim);
    auto advance = begin(late_backend);
    CHECK(applied.save(AppliedTargetState{target(), {}, 2, 0, {}, 0}, 0, *advance.transaction()));
    CHECK(advance.commit().outcome == CommitOutcome::Committed);
    auto late_ack = begin(late_backend);
    const auto obsolete_ack = repository.acknowledge_success(late_marker.id, late_marker.claim_token,
        late_marker.revision, ClockTime{}, std::string{"message:old"}, applied, *late_ack.transaction());
    CHECK(!obsolete_ack && obsolete_ack.error().code == ErrorCode::StorageStaleClaim);
    CHECK(late_ack.transaction()->state() == TransactionState::RolledBack);
    CHECK(read_effect(late_backend, late_marker.id).state == OutboxEffectState::DispatchAdmitted);

    InMemoryStorageBackend separate;
    const ConversationKey owners[] = {
        ConversationKey{BotId{7}, ChatId{42}, Chat{}},
        ConversationKey{BotId{8}, ChatId{42}, Chat{}},
        ConversationKey{BotId{7}, ChatId{43}, Chat{}}};
    for (const auto& owner : owners) {
        auto effect = make_effect("same-logical-slot", 1);
        const auto scoped = make_logical_target_identity(owner, "body");
        CHECK(scoped);
        effect.slot.target_identity = scoped.value();
        effect.id = OutboxEffectId::from_slot(effect.slot);
        insert_effect(separate, effect);
    }
    auto claims = begin(separate);
    const auto page = repository.claim_outbox_effects(3, ClockTime{}, 1h, {}, *claims.transaction());
    CHECK(page && page.value().effects.size() == 3);
    CHECK(claims.commit().outcome == CommitOutcome::Committed);
    for (const auto& item : page.value().effects) {
        const auto admitted = admit(separate, repository, item);
        CHECK(admitted.state == OutboxEffectState::DispatchAdmitted);
    }
}

void test_retry_budget_overflow_and_legacy_codec()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository(1, 2, 1);
    const auto effect = make_effect("budget", 1);
    insert_effect(backend, effect);
    const auto claim = claim_one(backend, repository);
    auto failure = begin(backend);
    const auto exhausted = repository.record_retryable_failure(effect.id, claim.claim_token,
        claim.revision, ClockTime{}, ClockTime{}, *failure.transaction());
    CHECK(exhausted && exhausted.value().state == OutboxEffectState::Failed);
    CHECK(failure.commit().outcome == CommitOutcome::Committed);

    auto overflow = make_effect("token-overflow", 1);
    overflow.claim_token = ClaimToken{std::numeric_limits<std::uint64_t>::max()};
    const auto encoded = encode_outbox_effect(overflow);
    CHECK(encoded);
    auto raw = begin(backend);
    CHECK(raw.transaction()->create_if_absent("outbox_effects", overflow.id.value, encoded.value()));
    CHECK(raw.commit().outcome == CommitOutcome::Committed);
    auto overflow_claim = begin(backend);
    const auto rejected = repository.claim_outbox_effects(1, ClockTime{}, 1h, {},
                                                          *overflow_claim.transaction());
    CHECK(!rejected && rejected.error().code == ErrorCode::StorageRevisionExhausted);
    CHECK(overflow_claim.transaction()->state() == TransactionState::RolledBack);

    InMemoryStorageBackend count_backend;
    auto count = make_effect("attempt-overflow", 1);
    count.attempt_count = std::numeric_limits<std::uint32_t>::max();
    count.state = OutboxEffectState::RetryableFailure;
    count.claim_token = ClaimToken{1};
    const auto count_bytes = encode_outbox_effect(count);
    CHECK(count_bytes);
    auto count_raw = begin(count_backend);
    CHECK(count_raw.transaction()->create_if_absent("outbox_effects", count.id.value, count_bytes.value()));
    CHECK(count_raw.commit().outcome == CommitOutcome::Committed);
    auto count_scan = begin(count_backend);
    const auto count_page = repository.claim_outbox_effects(1, ClockTime{}, 1h, {}, *count_scan.transaction());
    CHECK(count_page && count_page.value().effects.empty());
    CHECK(count_scan.commit().outcome == CommitOutcome::Committed);
    CHECK(read_effect(count_backend, count.id).attempt_count == std::numeric_limits<std::uint32_t>::max());
    CHECK(read_effect(count_backend, count.id).state == OutboxEffectState::Failed);

    const auto current = encode_outbox_effect(effect);
    CHECK(current && current.value().substr(0, 3) == "OE2");
    auto legacy = current.value();
    legacy.replace(0, 3, "OE1");
    legacy.pop_back();
    const auto decoded = decode_outbox_effect(legacy);
    CHECK(decoded && decoded.value().id == effect.id && !decoded.value().superseded_by);
    auto malformed = current.value();
    malformed.back() = '\x02';
    CHECK(!decode_outbox_effect(malformed));

    auto invalid = begin(backend);
    CHECK(!repository.claim_outbox_effects(0, ClockTime{}, 1h, {}, *invalid.transaction()));
    CHECK(invalid.transaction()->state() == TransactionState::RolledBack);
}

void test_retry_failure_and_supersession()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    const auto old_effect = make_effect("supersede-old", 1, "old");
    const auto new_effect = make_effect("supersede-new", 2, "new");
    insert_effect(backend, old_effect);
    insert_effect(backend, new_effect);

    auto claim_work = begin(backend);
    const auto page = repository.claim_outbox_effects(
        2, ClockTime{}, 1h, {}, *claim_work.transaction());
    CHECK(page && page.value().effects.size() == 2);
    OutboxEffect old_claim;
    OutboxEffect new_claim;
    for (const auto& item : page.value().effects) {
        if (item.id == old_effect.id) old_claim = item;
        if (item.id == new_effect.id) new_claim = item;
    }
    CHECK(old_claim.id == old_effect.id && new_claim.id == new_effect.id);
    CHECK(claim_work.commit().outcome == CommitOutcome::Committed);

    auto supersede_work = begin(backend);
    const auto superseded = repository.supersede_effect(
        old_effect.id, old_claim.claim_token, old_claim.revision, ClockTime{},
        new_effect.id, *supersede_work.transaction());
    CHECK(superseded && superseded.value().state == OutboxEffectState::Superseded
          && superseded.value().superseded_by
          && *superseded.value().superseded_by == new_effect.id);
    CHECK(supersede_work.commit().outcome == CommitOutcome::Committed);

    const auto admitted_new = admit(backend, repository, new_claim);
    auto failed_work = begin(backend);
    const auto failed = repository.record_failed(
        new_effect.id, admitted_new.claim_token, admitted_new.revision, ClockTime{},
        *failed_work.transaction());
    CHECK(failed && failed.value().state == OutboxEffectState::Failed);
    CHECK(failed_work.commit().outcome == CommitOutcome::Committed);

    const auto old_state = read_effect(backend, old_effect.id);
    const auto new_state = read_effect(backend, new_effect.id);
    CHECK(old_state.state == OutboxEffectState::Superseded
          && new_state.state == OutboxEffectState::Failed);

    const auto retry_effect = make_effect("definite-admission-rejection", 3);
    insert_effect(backend, retry_effect);
    const auto retry_claim = claim_one(backend, repository);
    const auto retry_admitted = admit(backend, repository, retry_claim);
    auto rejection = begin(backend);
    const auto retryable = repository.record_retryable_failure(
        retry_admitted.id, retry_admitted.claim_token, retry_admitted.revision,
        ClockTime{}, ClockTime{1h}, *rejection.transaction());
    CHECK(retryable && retryable.value().state == OutboxEffectState::RetryableFailure);
    CHECK(rejection.commit().outcome == CommitOutcome::Committed);
    const auto re_claim = claim_one(backend, repository, ClockTime{1h});
    CHECK(admit(backend, repository, re_claim, ClockTime{1h}).state == OutboxEffectState::DispatchAdmitted);
}

void test_bounded_scan_and_pending_shutdown_recovery()
{
    InMemoryStorageBackend backend;
    OutboxRepository repository(1, 1);
    insert_effect(backend, make_effect("bounded-a", 1, "a"));
    insert_effect(backend, make_effect("bounded-b", 1, "b"));

    auto first = begin(backend);
    const auto first_page = repository.claim_outbox_effects(
        1, ClockTime{}, 1h, {}, *first.transaction());
    CHECK(first_page && first_page.value().effects.size() == 1
          && first_page.value().examined == 1 && first_page.value().continuation);
    const auto continuation = *first_page.value().continuation;
    CHECK(first.commit().outcome == CommitOutcome::Committed);

    auto second = begin(backend);
    const auto second_page = repository.claim_outbox_effects(
        1, ClockTime{}, 1h, continuation, *second.transaction());
    CHECK(second_page && second_page.value().effects.size() == 1);
    CHECK(second.commit().outcome == CommitOutcome::Committed);

    // A pending effect remains recoverable when a worker stops before claiming it.
    const auto pending = make_effect("shutdown-pending", 1, "pending");
    insert_effect(backend, pending);
    CHECK(read_effect(backend, pending.id).state == OutboxEffectState::Pending);
}

} // namespace

int main()
{
    test_competing_claims_and_lease_fencing();
    test_dispatch_boundary_and_unknown_recovery();
    test_acknowledgement_is_atomic_and_monotonic();
    test_retry_failure_and_supersession();
    test_bounded_scan_and_pending_shutdown_recovery();
    test_target_ownership_and_late_completion();
    test_partial_staging_is_fail_closed();
    test_commit_failure_matrix_and_dispatch_gate();
    test_target_revision_watch_and_scope_isolation();
    test_retry_budget_overflow_and_legacy_codec();
    return 0;
}
