#include "imtg/reconciliation/outbox.hpp"
#include "imtg/sessions/session.hpp"
#include "imtg/storage/time_encoding.hpp"
#include "test_support.hpp"

#include <chrono>
#include <limits>
#include <string>

namespace {

using namespace imtg;

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

RenderIntentId publish_intent(InMemoryStorageBackend& backend, const ConversationKey& key,
                              StorageRevision session_revision = 2)
{
    RenderIntentRepository repository;
    RenderIntent intent{RenderIntentRepository::make_id(key, session_revision), key,
                        session_revision, "screen", std::nullopt};
    auto work = begin(backend);
    auto appended = repository.append(intent, *work.transaction());
    CHECK(appended && appended.value() == intent.id);
    const auto committed = work.commit();
    CHECK(committed.outcome == CommitOutcome::Committed);
    return intent.id;
}

RenderIntentClaim claim_intent(InMemoryStorageBackend& backend, const RenderIntentId& id,
                               ClockTime now)
{
    (void)id;
    RenderIntentClaimRepository claims;
    auto work = begin(backend);
    auto page = claims.claim_render_intents(1, now, std::chrono::hours(1), {}, *work.transaction());
    CHECK(page && page.value().claims.size() == 1);
    const auto committed = work.commit();
    CHECK(committed.outcome == CommitOutcome::Committed);
    return page.value().claims.front();
}

ReconciliationInput input_for(const ConversationKey& key, const RenderIntentId& id,
                              StorageRevision desired, StorageRevision applied,
                              StorageRevision applied_target_revision = 0,
                              std::uint64_t generation = 2,
                              StorageRevision generation_revision = 1)
{
    ReconciliationInput input{id, "logical-target", EffectKind::Edit, "body", desired, applied,
                              RenderPolicy::OnChange, 1, "hello"};
    input.conversation = key;
    input.session_revision = 2;
    input.desired_generation = generation;
    input.desired_generation_row_revision = generation_revision;
    input.applied_target_revision = applied_target_revision;
    return input;
}

void create_session_watch_row(InMemoryStorageBackend& backend, const ConversationKey& key,
                              StorageRevision revision = 2)
{
    auto work = begin(backend);
    const auto created = work.transaction()->create_if_absent("sessions", key.encode(), "session");
    CHECK(created && created.value() == 1);
    const auto first_commit = work.commit();
    CHECK(first_commit.outcome == CommitOutcome::Committed);
    for (StorageRevision current = 1; current < revision; ++current) {
        auto update = begin(backend);
        const auto changed = update.transaction()->write("sessions", key.encode(), "session", current);
        CHECK(changed && changed.value() == current + 1);
        const auto committed = update.commit();
        CHECK(committed.outcome == CommitOutcome::Committed);
    }
}

void save_applied(InMemoryStorageBackend& backend, const AppliedTargetState& state,
                  StorageRevision expected_revision)
{
    AppliedTargetRepository repository;
    auto work = begin(backend);
    auto saved = repository.save(state, expected_revision, *work.transaction());
    CHECK(saved);
    const auto committed = work.commit();
    CHECK(committed.outcome == CommitOutcome::Committed);
}

void test_codec_and_replay()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    const auto id = publish_intent(backend, key);
    ReconciliationPlanner planner;
    const auto input = input_for(key, id, 2, 1);
    auto plan = planner.build(input);
    CHECK(plan && !plan.value().no_op);
    CHECK(plan.value().effect.payload_digest == "fnv1a64:a430d84680aabd0b");
    auto encoded = encode_outbox_effect(plan.value().effect);
    CHECK(encoded);
    auto decoded = decode_outbox_effect(encoded.value());
    CHECK(decoded && decoded.value().payload == "hello");
    auto extreme = decode_storage_timestamp(std::numeric_limits<std::uint64_t>::max() >> 1);
    CHECK(!extreme && extreme.error().code == ErrorCode::InvalidEncoding);

    OutboxRepository repository;
    auto work = begin(backend);
    auto inserted = repository.create_or_validate(plan.value().effect, *work.transaction());
    CHECK(inserted);
    auto replay = repository.create_or_validate(plan.value().effect, *work.transaction());
    CHECK(replay && replay.value().id == inserted.value().id);
    auto conflict = plan.value().effect;
    conflict.payload = "different";
    auto different_input = input;
    different_input.canonical_payload = "different";
    auto different_plan = planner.build(different_input);
    CHECK(different_plan);
    conflict.payload_digest = different_plan.value().effect.payload_digest;
    auto rejected = repository.create_or_validate(conflict, *work.transaction());
    CHECK(!rejected && rejected.error().code == ErrorCode::StorageConflict);
    const auto committed = work.commit();
    CHECK(committed.outcome == CommitOutcome::Committed);

    const auto malformed = decode_outbox_effect(encoded.value().substr(0, encoded.value().size() - 1));
    CHECK(!malformed);
    auto binary_input = input;
    binary_input.canonical_payload = std::string("a\0b", 3);
    auto binary_plan = planner.build(binary_input);
    CHECK(binary_plan);
    auto binary_encoded = encode_outbox_effect(binary_plan.value().effect);
    CHECK(binary_encoded);
    auto binary_round_trip = decode_outbox_effect(binary_encoded.value());
    CHECK(binary_round_trip && binary_round_trip.value().payload == std::string("a\0b", 3));
    CHECK(!decode_outbox_effect(std::string("OE1", 3) + std::string(4 * 1024 * 1024, 'x')));
    auto no_op = planner.build(input_for(key, id, 2, 2, 0, 2, 1));
    CHECK(no_op && no_op.value().no_op && no_op.value().target_identity == "logical-target");
    auto always = input_for(key, id, 2, 2, 0, 2, 1);
    always.render_policy = RenderPolicy::Always;
    auto always_plan = planner.build(always);
    CHECK(always_plan && !always_plan.value().no_op);

    InMemoryStorageBackend applied_backend;
    save_applied(applied_backend, AppliedTargetState{"logical-target", std::nullopt, 3, 2, std::nullopt, 0}, 0);
    auto applied_work = begin(applied_backend);
    AppliedTargetRepository applied_repository;
    auto decreased = applied_repository.save(
        AppliedTargetState{"logical-target", std::nullopt, 3, 1, std::nullopt, 1}, 1,
        *applied_work.transaction());
    CHECK(!decreased && decreased.error().code == ErrorCode::StorageConflict);
    applied_work.rollback();
}

void test_successful_publish_and_no_op()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    const auto now = ClockTime{};
    const auto claim = claim_intent(backend, id, now);
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, 2, 1));
    CHECK(plan);
    RenderIntentClaimRepository claims;
    OutboxRepository outbox;
    AtomicReconciliation publisher(backend, claims, outbox);
    auto published = publisher.publish(plan.value(), id, claim.claim_token, claim.row_revision, now);
    CHECK(published.outcome == ReconciliationOutcome::Planned && published.effect.has_value());

    InMemoryStorageBackend mismatch_backend;
    create_session_watch_row(mismatch_backend, key);
    const auto mismatch_id = publish_intent(mismatch_backend, key);
    const auto mismatch_claim = claim_intent(mismatch_backend, mismatch_id, now);
    auto mismatch_plan = planner.build(input_for(key, mismatch_id, 2, 1));
    CHECK(mismatch_plan);
    mismatch_plan.value().render_intent_id = RenderIntentId{"other-intent"};
    AtomicReconciliation mismatch_publisher(mismatch_backend, claims, outbox);
    auto mismatch = mismatch_publisher.publish(mismatch_plan.value(), mismatch_id,
                                               mismatch_claim.claim_token, mismatch_claim.row_revision, now);
    CHECK(mismatch.outcome == ReconciliationOutcome::RejectedConflict);

    InMemoryStorageBackend no_op_backend;
    create_session_watch_row(no_op_backend, key);
    const auto no_op_id = publish_intent(no_op_backend, key);
    save_applied(no_op_backend, AppliedTargetState{"logical-target", std::nullopt, 2, 2, std::nullopt, 0}, 0);
    const auto no_op_claim = claim_intent(no_op_backend, no_op_id, now);
    auto no_op_plan = planner.build(input_for(key, no_op_id, 2, 2, 1, 2, 1));
    CHECK(no_op_plan && no_op_plan.value().no_op);
    AtomicReconciliation no_op_publisher(no_op_backend, claims, outbox);
    auto completed = no_op_publisher.publish(no_op_plan.value(), no_op_id,
                                              no_op_claim.claim_token, no_op_claim.row_revision, now);
    CHECK(completed.outcome == ReconciliationOutcome::AlreadyApplied);
}

void test_fencing_and_unknown_recovery()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    const auto now = ClockTime{};
    const auto claim = claim_intent(backend, id, now);
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, 2, 1));
    CHECK(plan);
    RenderIntentClaimRepository claims;
    OutboxRepository outbox;
    AtomicReconciliation publisher(backend, claims, outbox);
    auto stale = publisher.publish(plan.value(), id, ClaimToken{claim.claim_token.value + 1}, claim.row_revision, now);
    CHECK(stale.outcome == ReconciliationOutcome::RejectedConflict);
    auto expired = publisher.publish(plan.value(), id, claim.claim_token, claim.row_revision,
                                    now + std::chrono::hours(2));
    CHECK(expired.outcome == ReconciliationOutcome::RejectedConflict);

    InMemoryFailureInjection before;
    before.unknown_before_publish = true;
    backend.set_failure_injection(before);
    auto unknown_before = publisher.publish(plan.value(), id, claim.claim_token, claim.row_revision, now);
    CHECK(unknown_before.outcome == ReconciliationOutcome::Unknown);
    auto recovered_before = publisher.inspect_unknown(plan.value(), id, now);
    CHECK(recovered_before && recovered_before.value().outcome == ReconciliationRecoveryOutcome::NotPublished);

    InMemoryStorageBackend after_backend;
    create_session_watch_row(after_backend, key);
    const auto id_after = publish_intent(after_backend, key);
    const auto claim_after = claim_intent(after_backend, id_after, now);
    auto plan_after = planner.build(input_for(key, id_after, 2, 1));
    CHECK(plan_after);
    InMemoryFailureInjection after;
    after.unknown_after_publish = true;
    after_backend.set_failure_injection(after);
    AtomicReconciliation after_publisher(after_backend, claims, outbox);
    auto unknown_after = after_publisher.publish(plan_after.value(), id_after, claim_after.claim_token,
                                           claim_after.row_revision, now);
    CHECK(unknown_after.outcome == ReconciliationOutcome::Unknown);
    auto recovered_after = after_publisher.inspect_unknown(plan_after.value(), id_after, now);
    CHECK(recovered_after && recovered_after.value().outcome == ReconciliationRecoveryOutcome::Published
          && recovered_after.value().effect.has_value());
}

void test_generation_and_applied_conflicts()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    const auto claim = claim_intent(backend, id, ClockTime{});
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, 2, 1));
    CHECK(plan);
    RenderIntentClaimRepository generation_claims;
    OutboxRepository generation_outbox;
    AtomicReconciliation publisher(backend, generation_claims, generation_outbox);
    auto tx2 = begin(backend);
    auto staged = publisher.stage(plan.value(), id, claim.claim_token, claim.row_revision, ClockTime{}, *tx2.transaction());
    CHECK(staged && staged.value().has_value());
    auto newer_generation = begin(backend);
    DesiredGenerationRepository generations;
    auto advanced = generations.advance(key.encode(), 2, 1, *newer_generation.transaction());
    CHECK(advanced);
    const auto advanced_commit = newer_generation.commit();
    CHECK(advanced_commit.outcome == CommitOutcome::Committed);
    const auto rejected_commit = tx2.commit();
    CHECK(rejected_commit.outcome == CommitOutcome::RejectedConflict);
    auto after_conflict = begin(backend, TransactionMode::ReadOnly);
    OutboxRepository after_conflict_outbox;
    auto absent = after_conflict_outbox.load(plan.value().effect.id, *after_conflict.transaction());
    CHECK(absent && !absent.value());
    auto rejected = publisher.publish(plan.value(), id, claim.claim_token, claim.row_revision, ClockTime{});
    CHECK(rejected.outcome == ReconciliationOutcome::RejectedConflict);

    InMemoryStorageBackend target_backend;
    create_session_watch_row(target_backend, key);
    const auto target_id = publish_intent(target_backend, key);
    const auto target_claim = claim_intent(target_backend, target_id, ClockTime{});
    save_applied(target_backend, AppliedTargetState{"logical-target", std::nullopt, 2, 0, std::nullopt, 0}, 0);
    auto target_plan = planner.build(input_for(key, target_id, 2, 0, 1, 2, 1));
    CHECK(target_plan);
    RenderIntentClaimRepository target_claims;
    OutboxRepository target_outbox;
    AtomicReconciliation target_publisher(target_backend, target_claims, target_outbox);
    auto target_tx2 = begin(target_backend);
    auto target_staged = target_publisher.stage(target_plan.value(), target_id, target_claim.claim_token,
                                                target_claim.row_revision, ClockTime{}, *target_tx2.transaction());
    CHECK(target_staged && target_staged.value().has_value());
    auto target_update = begin(target_backend);
    AppliedTargetRepository applied;
    auto changed = applied.save(AppliedTargetState{"logical-target", std::nullopt, 2, 1, std::nullopt, 1},
                                1, *target_update.transaction());
    CHECK(changed);
    const auto target_commit = target_update.commit();
    CHECK(target_commit.outcome == CommitOutcome::Committed);
    const auto target_rejected_commit = target_tx2.commit();
    CHECK(target_rejected_commit.outcome == CommitOutcome::RejectedConflict);
}

} // namespace

int main()
{
    test_codec_and_replay();
    test_successful_publish_and_no_op();
    test_fencing_and_unknown_recovery();
    test_generation_and_applied_conflicts();
    return 0;
}
