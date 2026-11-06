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

std::string target_for(const ConversationKey& key, std::string_view slot = "logical-target")
{
    const auto encoded = make_logical_target_identity(key, slot);
    CHECK(encoded);
    return encoded.value();
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
                              const RenderIntentClaim& claim,
                              StorageRevision desired, StorageRevision applied,
                              StorageRevision applied_target_revision = 0,
                              std::uint64_t generation = 2,
                              StorageRevision generation_revision = 1)
{
    const auto target = target_for(key);
    ReconciliationInput input{id, target, EffectKind::Edit, "body", desired, applied,
                              RenderPolicy::OnChange, 1, "hello"};
    input.conversation = key;
    input.session_revision = 2;
    input.desired_generation = generation;
    input.desired_generation_row_revision = generation_revision;
    input.applied_target_revision = applied_target_revision;
    input.context = ReconciliationContext{
        id, key, 2, desired, generation, generation_revision, target,
        EffectKind::Edit, "body", applied_target_revision, applied, RenderPolicy::OnChange,
        claim.claim_token, claim.row_revision};
    return input;
}

void create_session_watch_row(InMemoryStorageBackend& backend, const ConversationKey& key,
                              StorageRevision revision = 2)
{
    SessionRecord record;
    record.conversation = key;
    record.state = FsmStateId{"state", 1};
    record.screen = ScreenIdentity{"screen", 1};
    const auto encoded = encode_session_record(record);
    CHECK(encoded);
    auto work = begin(backend);
    const auto created = work.transaction()->create_if_absent("sessions", key.encode(), encoded.value());
    CHECK(created && created.value() == 1);
    const auto first_commit = work.commit();
    CHECK(first_commit.outcome == CommitOutcome::Committed);
    for (StorageRevision current = 1; current < revision; ++current) {
        auto update = begin(backend);
        const auto changed = update.transaction()->write("sessions", key.encode(), encoded.value(), current);
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

class RecordingOutboxRepository final : public IOutboxRepository {
public:
    explicit RecordingOutboxRepository(const OutboxRepository& delegate) noexcept
        : m_delegate(delegate) {}

    Result<std::optional<OutboxEffect>> load(
        const OutboxEffectId& id, ITransaction& transaction) const override
    {
        ++load_calls;
        return m_delegate.load(id, transaction);
    }

    Result<OutboxEffect> create_or_validate(
        const OutboxEffect& effect, ITransaction& transaction) const override
    {
        return m_delegate.create_or_validate(effect, transaction);
    }

    mutable std::size_t load_calls{};

private:
    const OutboxRepository& m_delegate;
};

void test_codec_and_replay()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    save_applied(backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto claim = claim_intent(backend, id, ClockTime{});
    ReconciliationPlanner planner;
    const auto input = input_for(key, id, claim, 2, 1);
    auto plan = planner.build(input);
    CHECK(plan && !plan.value().no_op);
    auto incomplete = input;
    incomplete.context.reset();
    CHECK(!planner.build(incomplete));
    auto forged_context = input;
    forged_context.context->conversation = ConversationKey{BotId{8}, ChatId{42}, Chat{}};
    forged_context.target_identity = target_for(forged_context.context->conversation);
    forged_context.context->target_identity = forged_context.target_identity;
    CHECK(!planner.build(forged_context));
    forged_context.conversation.reset();
    auto forged_plan = planner.build(forged_context);
    CHECK(forged_plan);
    RenderIntentClaimRepository forged_claims;
    OutboxRepository forged_outbox;
    AtomicReconciliation forged_publisher(backend, forged_claims, forged_outbox);
    const auto forged_result = forged_publisher.publish(
        forged_plan.value(), id, claim.claim_token, claim.row_revision, ClockTime{});
    CHECK(forged_result.outcome == ReconciliationOutcome::RejectedConflict);
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
    auto no_op = planner.build(input_for(key, id, claim, 2, 2, 0, 2, 1));
    CHECK(no_op && no_op.value().no_op && no_op.value().target_identity == target_for(key));
    auto always = input_for(key, id, claim, 2, 2, 0, 2, 1);
    always.render_policy = RenderPolicy::Always;
    always.context->render_policy = RenderPolicy::Always;
    auto always_plan = planner.build(always);
    CHECK(always_plan && !always_plan.value().no_op);
    auto never = input;
    never.render_policy = RenderPolicy::Never;
    never.context->render_policy = RenderPolicy::Never;
    CHECK(!planner.build(never));
    auto invalid_policy = input;
    invalid_policy.render_policy = static_cast<RenderPolicy>(99);
    invalid_policy.context->render_policy = static_cast<RenderPolicy>(99);
    CHECK(!planner.build(invalid_policy));
    auto invalid_effect_kind = input;
    invalid_effect_kind.effect_kind = static_cast<EffectKind>(99);
    invalid_effect_kind.context->effect_kind = static_cast<EffectKind>(99);
    CHECK(!planner.build(invalid_effect_kind));

    InMemoryStorageBackend applied_backend;
    save_applied(applied_backend, AppliedTargetState{target_for(key), std::nullopt, 3, 2, std::nullopt, 0}, 0);
    auto applied_work = begin(applied_backend);
    AppliedTargetRepository applied_repository;
    auto raw_target = begin(applied_backend, TransactionMode::ReadOnly);
    const auto legacy_target = applied_repository.load("logical-target", *raw_target.transaction());
    CHECK(!legacy_target);
    auto decreased = applied_repository.save(
        AppliedTargetState{target_for(key), std::nullopt, 3, 1, std::nullopt, 1}, 1,
        *applied_work.transaction());
    CHECK(!decreased && decreased.error().code == ErrorCode::StorageConflict);
    applied_work.rollback();
    auto desired_decreased = begin(applied_backend);
    auto desired_regression = applied_repository.save(
        AppliedTargetState{target_for(key), std::nullopt, 2, 2, std::nullopt, 1}, 1,
        *desired_decreased.transaction());
    CHECK(!desired_regression && desired_regression.error().code == ErrorCode::StorageConflict);
    desired_decreased.rollback();
}

void test_target_scope_isolation()
{
    const auto key_a = conversation();
    const auto key_b = ConversationKey{BotId{8}, ChatId{42}, Chat{}};
    const auto key_c = ConversationKey{BotId{7}, ChatId{43}, Chat{}};
    const auto target_a = target_for(key_a);
    const auto target_b = target_for(key_b);
    const auto target_c = target_for(key_c);
    const auto target_a_secondary = target_for(key_a, "secondary");
    CHECK(target_a != target_b);
    CHECK(target_a != target_c);
    CHECK(target_a != target_a_secondary);
    CHECK(target_b != target_c);

    InMemoryStorageBackend backend;
    save_applied(backend,
                 AppliedTargetState{target_a, std::nullopt, 2, 2, std::nullopt, 0}, 0);
    save_applied(backend,
                 AppliedTargetState{target_b, std::nullopt, 2, 0, std::nullopt, 0}, 0);
    save_applied(backend,
                 AppliedTargetState{target_a_secondary, std::nullopt, 1, 0, std::nullopt, 0}, 0);

    AppliedTargetRepository applied;
    auto read = begin(backend, TransactionMode::ReadOnly);
    const auto a_state = applied.load(target_a, *read.transaction());
    const auto b_state = applied.load(target_b, *read.transaction());
    const auto secondary_state = applied.load(target_a_secondary, *read.transaction());
    CHECK(a_state && a_state.value() && a_state.value()->confirmed_applied_revision == 2);
    CHECK(b_state && b_state.value() && b_state.value()->confirmed_applied_revision == 0);
    CHECK(secondary_state && secondary_state.value());

    create_session_watch_row(backend, key_a);
    create_session_watch_row(backend, key_b);
    const auto id_a = publish_intent(backend, key_a);
    const auto id_b = publish_intent(backend, key_b);
    const auto claim_a = claim_intent(backend, id_a, ClockTime{});
    const auto claim_b = claim_intent(backend, id_b, ClockTime{});
    ReconciliationPlanner planner;
    auto a_plan = planner.build(input_for(key_a, id_a, claim_a, 2, 2, 1));
    auto b_plan = planner.build(input_for(key_b, id_b, claim_b, 2, 0, 1));
    CHECK(a_plan && a_plan.value().no_op);
    CHECK(b_plan && !b_plan.value().no_op);

    RenderIntentClaimRepository claims;
    OutboxRepository outbox;
    AtomicReconciliation publisher(backend, claims, outbox);
    const auto a_result = publisher.publish(
        a_plan.value(), id_a, claim_a.claim_token, claim_a.row_revision, ClockTime{});
    const auto b_result = publisher.publish(
        b_plan.value(), id_b, claim_b.claim_token, claim_b.row_revision, ClockTime{});
    CHECK(a_result.outcome == ReconciliationOutcome::AlreadyApplied);
    CHECK(b_result.outcome == ReconciliationOutcome::Planned);
}

void test_successful_publish_and_no_op()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    save_applied(backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto now = ClockTime{};
    const auto claim = claim_intent(backend, id, now);
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, claim, 2, 1, 1));
    CHECK(plan);
    RenderIntentClaimRepository claims;
    OutboxRepository outbox;
    AtomicReconciliation publisher(backend, claims, outbox);
    auto published = publisher.publish(plan.value(), id, claim.claim_token, claim.row_revision, now);
    CHECK(published.outcome == ReconciliationOutcome::Planned && published.effect.has_value());

    InMemoryStorageBackend mismatch_backend;
    create_session_watch_row(mismatch_backend, key);
    const auto mismatch_id = publish_intent(mismatch_backend, key);
    save_applied(mismatch_backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto mismatch_claim = claim_intent(mismatch_backend, mismatch_id, now);
    auto mismatch_plan = planner.build(input_for(key, mismatch_id, mismatch_claim, 2, 1, 1));
    CHECK(mismatch_plan);
    mismatch_plan.value().render_intent_id = RenderIntentId{"other-intent"};
    AtomicReconciliation mismatch_publisher(mismatch_backend, claims, outbox);
    auto mismatch = mismatch_publisher.publish(mismatch_plan.value(), mismatch_id,
                                               mismatch_claim.claim_token, mismatch_claim.row_revision, now);
    CHECK(mismatch.outcome == ReconciliationOutcome::RejectedConflict);

    InMemoryStorageBackend no_op_backend;
    create_session_watch_row(no_op_backend, key);
    const auto no_op_id = publish_intent(no_op_backend, key);
    save_applied(no_op_backend, AppliedTargetState{target_for(key), std::nullopt, 2, 2, std::nullopt, 0}, 0);
    const auto no_op_claim = claim_intent(no_op_backend, no_op_id, now);
    auto no_op_plan = planner.build(input_for(key, no_op_id, no_op_claim, 2, 2, 1, 2, 1));
    CHECK(no_op_plan && no_op_plan.value().no_op);
    AtomicReconciliation no_op_publisher(no_op_backend, claims, outbox);
    auto completed = no_op_publisher.publish(no_op_plan.value(), no_op_id,
                                              no_op_claim.claim_token, no_op_claim.row_revision, now);
    CHECK(completed.outcome == ReconciliationOutcome::AlreadyApplied);

    InMemoryStorageBackend planned_no_op_backend;
    create_session_watch_row(planned_no_op_backend, key);
    const auto planned_no_op_id = publish_intent(planned_no_op_backend, key);
    save_applied(planned_no_op_backend,
                 AppliedTargetState{target_for(key), std::nullopt, 2, 2, std::nullopt, 0}, 0);
    const auto planned_no_op_claim = claim_intent(planned_no_op_backend, planned_no_op_id, now);
    auto planned_no_op_plan = planner.build(
        input_for(key, planned_no_op_id, planned_no_op_claim, 2, 2, 1, 2, 1));
    CHECK(planned_no_op_plan && planned_no_op_plan.value().no_op);
    auto planned_work = begin(planned_no_op_backend);
    auto planned_transition = claims.plan_render_intent(
        planned_no_op_id, planned_no_op_claim.claim_token,
        planned_no_op_claim.row_revision, now, *planned_work.transaction());
    CHECK(planned_transition);
    CHECK(planned_work.commit().outcome == CommitOutcome::Committed);
    AtomicReconciliation planned_no_op_publisher(planned_no_op_backend, claims, outbox);
    auto planned_recovery = planned_no_op_publisher.inspect_unknown(
        planned_no_op_plan.value(), planned_no_op_id, now);
    CHECK(planned_recovery
          && planned_recovery.value().outcome == ReconciliationRecoveryOutcome::Inconclusive);
}

void test_fencing_and_unknown_recovery()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    save_applied(backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto now = ClockTime{};
    const auto claim = claim_intent(backend, id, now);
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, claim, 2, 1, 1));
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

    auto renewed_work = begin(backend);
    const auto renewed = claims.renew_render_intent(
        id, claim.claim_token, claim.row_revision, now, now + std::chrono::hours(2),
        *renewed_work.transaction());
    CHECK(renewed);
    CHECK(renewed_work.commit().outcome == CommitOutcome::Committed);
    auto replaced_recovery = publisher.inspect_unknown(plan.value(), id, now);
    CHECK(replaced_recovery
          && replaced_recovery.value().outcome == ReconciliationRecoveryOutcome::Inconclusive);

    auto different_input = input_for(key, id, renewed.value(), 2, 1, 1);
    different_input.canonical_payload = "different";
    auto different_plan = planner.build(different_input);
    CHECK(different_plan);
    auto later_publish = publisher.publish(different_plan.value(), id,
                                           renewed.value().claim_token,
                                           renewed.value().row_revision, now);
    CHECK(later_publish.outcome == ReconciliationOutcome::Planned);
    auto conflicting_recovery = publisher.inspect_unknown(plan.value(), id, now);
    CHECK(conflicting_recovery
          && conflicting_recovery.value().outcome == ReconciliationRecoveryOutcome::Inconclusive);

    InMemoryStorageBackend same_effect_backend;
    create_session_watch_row(same_effect_backend, key);
    const auto same_effect_id = publish_intent(same_effect_backend, key);
    save_applied(same_effect_backend,
                 AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto same_effect_claim = claim_intent(same_effect_backend, same_effect_id, now);
    auto same_effect_plan = planner.build(
        input_for(key, same_effect_id, same_effect_claim, 2, 1, 1));
    CHECK(same_effect_plan);
    InMemoryFailureInjection same_effect_failure;
    same_effect_failure.unknown_before_publish = true;
    same_effect_backend.set_failure_injection(same_effect_failure);
    AtomicReconciliation same_effect_publisher(same_effect_backend, claims, outbox);
    const auto first_attempt = same_effect_publisher.publish(
        same_effect_plan.value(), same_effect_id, same_effect_claim.claim_token,
        same_effect_claim.row_revision, now);
    CHECK(first_attempt.outcome == ReconciliationOutcome::Unknown);
    auto same_effect_renew = begin(same_effect_backend);
    const auto renewed_same_effect = claims.renew_render_intent(
        same_effect_id, same_effect_claim.claim_token, same_effect_claim.row_revision,
        now, now + std::chrono::hours(2), *same_effect_renew.transaction());
    CHECK(renewed_same_effect);
    CHECK(same_effect_renew.commit().outcome == CommitOutcome::Committed);
    auto retry_same_effect = planner.build(
        input_for(key, same_effect_id, renewed_same_effect.value(), 2, 1, 1));
    CHECK(retry_same_effect);
    const auto retry_result = same_effect_publisher.publish(
        retry_same_effect.value(), same_effect_id, renewed_same_effect.value().claim_token,
        renewed_same_effect.value().row_revision, now);
    CHECK(retry_result.outcome == ReconciliationOutcome::Planned);
    const auto same_effect_recovery = same_effect_publisher.inspect_unknown(
        same_effect_plan.value(), same_effect_id, now);
    CHECK(same_effect_recovery
          && same_effect_recovery.value().outcome == ReconciliationRecoveryOutcome::Inconclusive);

    InMemoryStorageBackend after_backend;
    create_session_watch_row(after_backend, key);
    const auto id_after = publish_intent(after_backend, key);
    save_applied(after_backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto claim_after = claim_intent(after_backend, id_after, now);
    auto plan_after = planner.build(input_for(key, id_after, claim_after, 2, 1, 1));
    CHECK(plan_after);
    InMemoryFailureInjection after;
    after.unknown_after_publish = true;
    after_backend.set_failure_injection(after);
    RecordingOutboxRepository recording_outbox(outbox);
    AtomicReconciliation after_publisher(after_backend, claims, recording_outbox);
    auto unknown_after = after_publisher.publish(plan_after.value(), id_after, claim_after.claim_token,
                                           claim_after.row_revision, now);
    CHECK(unknown_after.outcome == ReconciliationOutcome::Unknown);
    auto recovered_after = after_publisher.inspect_unknown(plan_after.value(), id_after, now);
    CHECK(recovered_after && recovered_after.value().outcome == ReconciliationRecoveryOutcome::Published
          && recovered_after.value().effect.has_value());
    CHECK(recording_outbox.load_calls > 0);
}

void test_stage_rolls_back_after_claim_failure()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    save_applied(backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto now = ClockTime{};
    const auto claim = claim_intent(backend, id, now);
    RenderIntentClaimRepository claims;
    OutboxRepository outbox;
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, claim, 2, 1, 1));
    CHECK(plan);

    auto renew = begin(backend);
    const auto renewed = claims.renew_render_intent(
        id, claim.claim_token, claim.row_revision, now, now + std::chrono::hours(2),
        *renew.transaction());
    CHECK(renewed);
    CHECK(renew.commit().outcome == CommitOutcome::Committed);

    AtomicReconciliation publisher(backend, claims, outbox);
    auto tx2 = begin(backend);
    const auto staged = publisher.stage(plan.value(), id, claim.claim_token,
                                        claim.row_revision, now, *tx2.transaction());
    CHECK(!staged && staged.error().code == ErrorCode::StorageStaleClaim);
    CHECK(tx2.transaction()->state() == TransactionState::RolledBack);
    CHECK(tx2.commit().outcome == CommitOutcome::Failed);

    auto verify = begin(backend, TransactionMode::ReadOnly);
    auto effect = outbox.load(plan.value().effect.id, *verify.transaction());
    CHECK(effect && !effect.value());
}

void test_generation_and_applied_conflicts()
{
    InMemoryStorageBackend backend;
    const auto key = conversation();
    create_session_watch_row(backend, key);
    const auto id = publish_intent(backend, key);
    save_applied(backend, AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto claim = claim_intent(backend, id, ClockTime{});
    ReconciliationPlanner planner;
    auto plan = planner.build(input_for(key, id, claim, 2, 1, 1));
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

    InMemoryStorageBackend session_backend;
    create_session_watch_row(session_backend, key);
    const auto session_id = publish_intent(session_backend, key);
    save_applied(session_backend,
                 AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 0}, 0);
    const auto session_claim = claim_intent(session_backend, session_id, ClockTime{});
    auto session_plan = planner.build(input_for(key, session_id, session_claim, 2, 1, 1));
    CHECK(session_plan);
    AtomicReconciliation session_publisher(session_backend, generation_claims, generation_outbox);
    auto session_tx2 = begin(session_backend);
    auto session_staged = session_publisher.stage(
        session_plan.value(), session_id, session_claim.claim_token,
        session_claim.row_revision, ClockTime{}, *session_tx2.transaction());
    CHECK(session_staged && session_staged.value().has_value());
    SessionRepository sessions;
    auto session_update = begin(session_backend);
    auto session_record = sessions.load(key, *session_update.transaction());
    CHECK(session_record);
    CHECK(session_record.value().has_value());
    session_record.value()->screen.version = 2;
    auto saved_session = sessions.save(*session_record.value(), 2, *session_update.transaction());
    CHECK(saved_session);
    CHECK(session_update.commit().outcome == CommitOutcome::Committed);
    CHECK(session_tx2.commit().outcome == CommitOutcome::RejectedConflict);
    auto session_verify = begin(session_backend, TransactionMode::ReadOnly);
    auto session_effect = generation_outbox.load(session_plan.value().effect.id, *session_verify.transaction());
    CHECK(session_effect && !session_effect.value());

    InMemoryStorageBackend target_backend;
    create_session_watch_row(target_backend, key);
    const auto target_id = publish_intent(target_backend, key);
    const auto target_claim = claim_intent(target_backend, target_id, ClockTime{});
    save_applied(target_backend, AppliedTargetState{target_for(key), std::nullopt, 2, 0, std::nullopt, 0}, 0);
    auto target_plan = planner.build(input_for(key, target_id, target_claim, 2, 0, 1, 2, 1));
    CHECK(target_plan);
    RenderIntentClaimRepository target_claims;
    OutboxRepository target_outbox;
    AtomicReconciliation target_publisher(target_backend, target_claims, target_outbox);
    auto forged_applied_plan = planner.build(
        input_for(key, target_id, target_claim, 2, 1, 1, 2, 1));
    CHECK(forged_applied_plan);
    auto forged_applied = target_publisher.publish(
        forged_applied_plan.value(), target_id, target_claim.claim_token,
        target_claim.row_revision, ClockTime{});
    CHECK(forged_applied.outcome == ReconciliationOutcome::RejectedConflict);
    auto target_tx2 = begin(target_backend);
    auto target_staged = target_publisher.stage(target_plan.value(), target_id, target_claim.claim_token,
                                                target_claim.row_revision, ClockTime{}, *target_tx2.transaction());
    CHECK(target_staged && target_staged.value().has_value());
    auto target_update = begin(target_backend);
    AppliedTargetRepository applied;
    auto changed = applied.save(AppliedTargetState{target_for(key), std::nullopt, 2, 1, std::nullopt, 1},
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
    test_target_scope_isolation();
    test_successful_publish_and_no_op();
    test_fencing_and_unknown_recovery();
    test_stage_rolls_back_after_claim_failure();
    test_generation_and_applied_conflicts();
    return 0;
}
