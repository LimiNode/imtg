#ifndef IMTG_RECONCILIATION_OUTBOX_HPP
#define IMTG_RECONCILIATION_OUTBOX_HPP

/// \file imtg/reconciliation/outbox.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include <imtg/core.hpp>
#include <imtg/sessions.hpp>

#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imtg {

/// \brief Kind of deterministic external operation represented by an outbox row.
enum class EffectKind : std::uint8_t { Send = 0, Edit = 1, Delete = 2, Replace = 3 };

/// \brief Canonical identity of one logical target within one conversation.
/// \details The encoded form binds the stable logical slot to the complete
/// \c ConversationKey. It is independent of a physical Telegram message ID.
struct LogicalTargetIdentity {
    ConversationKey conversation; ///< Bot/chat/session scope owning the target.
    std::string logical_slot; ///< Stable application-defined slot before delivery.

    /// \brief Encodes the target using the versioned, length-prefixed LT1 format.
    /// \return Binary-safe canonical storage identity.
    [[nodiscard]] Result<std::string> encode() const;

    /// \brief Compares target ownership and logical slot.
    friend bool operator==(const LogicalTargetIdentity& left,
                           const LogicalTargetIdentity& right) noexcept
    {
        return left.conversation == right.conversation
            && left.logical_slot == right.logical_slot;
    }
};

/// \brief Builds a canonical conversation-scoped target identity.
/// \param conversation Bot/chat/session scope owning the target.
/// \param logical_slot Stable slot name before a physical message exists.
/// \return Versioned LT1 identity, or a bounded-input error.
[[nodiscard]] Result<std::string> make_logical_target_identity(
    const ConversationKey& conversation, std::string_view logical_slot);

/// \brief Decodes and validates a canonical target identity.
/// \param encoded LT1 identity produced by make_logical_target_identity().
/// \return Parsed owner and logical slot, or an encoding/size error.
[[nodiscard]] Result<LogicalTargetIdentity> decode_logical_target_identity(
    std::string_view encoded);

/// \brief Stable logical slot for one externally observable operation.
/// \details Payload bytes are deliberately not part of this identity.
struct LogicalEffectSlot {
    RenderIntentId render_intent_id; ///< Owning render intent identity.
    std::string target_identity; ///< Canonical LT1 conversation-scoped target identity.
    EffectKind effect_kind = EffectKind::Replace; ///< Operation kind.
    std::string effect_position; ///< Stable discriminator within the target.

    /// \return Canonical identity bytes.
    [[nodiscard]] std::string encode() const;
    /// \brief Compares all logical slot components.
    friend bool operator==(const LogicalEffectSlot& a, const LogicalEffectSlot& b) noexcept {
        return a.render_intent_id == b.render_intent_id && a.target_identity == b.target_identity
            && a.effect_kind == b.effect_kind && a.effect_position == b.effect_position;
    }
};

/// \brief Stable identifier derived from a logical effect slot.
struct OutboxEffectId {
    std::string value; ///< Canonical stable identifier.
    /// \param slot Logical slot.
    /// \return Stable ID derived solely from the slot.
    [[nodiscard]] static OutboxEffectId from_slot(const LogicalEffectSlot& slot);
    /// \return Canonical ID bytes.
    [[nodiscard]] std::string encode() const { return value; }
    /// \brief Compares effect identifiers.
    friend bool operator==(const OutboxEffectId& a, const OutboxEffectId& b) noexcept {
        return a.value == b.value;
    }
};

/// \brief Durable lifecycle state of one externally observable effect.
enum class OutboxEffectState : std::uint8_t {
    Pending, ///< No active delivery claim exists.
    ClaimedBeforeDispatch, ///< A worker owns a live claim before transport admission.
    DispatchAdmitted, ///< The pre-dispatch boundary was committed before transport.
    Succeeded, ///< External success was confirmed and applied state was advanced.
    RetryableFailure, ///< The adapter proved that the request was not accepted.
    UnknownOutcome, ///< The external result is ambiguous; blind retry is forbidden.
    Failed, ///< A terminal non-retryable outcome was recorded.
    Superseded, ///< A newer effect replaced this effect under an explicit policy.
};

/// \brief Durable, transport-neutral effect payload and lifecycle metadata.
struct OutboxEffect {
    OutboxEffectId id; ///< Stable effect identity.
    LogicalEffectSlot slot; ///< Logical unique slot.
    std::uint32_t payload_version = 1; ///< Canonical payload schema version.
    std::string payload; ///< Canonical transport-neutral payload.
    std::string payload_digest; ///< Digest of payload bytes.
    StorageRevision desired_revision{}; ///< Desired session revision.
    OutboxEffectState state = OutboxEffectState::Pending; ///< Delivery lifecycle state.
    ClaimToken claim_token{}; ///< Delivery fencing token.
    ClockTime lease_until{}; ///< Delivery lease expiry.
    ClockTime next_eligible_at{}; ///< Earliest retry time.
    std::uint32_t attempt_count{}; ///< Number of claims, including pre-dispatch lease recovery.
    StorageRevision revision{}; ///< Backend row revision.
    std::optional<OutboxEffectId> superseded_by; ///< Newer effect retained for audit.
};

/// \brief Typed evidence supplied by an authoritative delivery-recovery layer.
/// \details This is intentionally narrower than a transport result: the caller
/// must identify the exact immutable effect attempt and its LT1 target. The
/// repository still verifies the durable claim, DT1 owner, and monotonic
/// applied-target state before publishing success.
struct UnknownSuccessEvidence {
    OutboxEffectId effect_id; ///< Exact effect whose UnknownOutcome was inspected.
    ClaimToken claim_token{}; ///< Claim token recorded for that attempt.
    StorageRevision expected_revision{}; ///< Effect row revision observed by recovery.
    StorageRevision expected_target_revision{}; ///< Applied-target row revision, or zero when absent.
    std::string target_identity; ///< Exact LT1 target identity returned by recovery.
    std::optional<std::string> physical_identity; ///< Confirmed transport identity, when available.
};

/// \brief Bounded page returned by an outbox claim scan.
struct OutboxEffectPage {
    std::vector<OutboxEffect> effects; ///< Effects claimed by this transaction.
    std::optional<std::string> continuation; ///< Opaque cursor; absent means end of scan.
    std::size_t examined{}; ///< Number of effect rows examined by this operation.
};

/// \brief Encodes an effect in a bounded versioned format.
/// \param effect Immutable identity and lifecycle state to validate and encode.
/// \return Binary envelope or a validation/size error.
[[nodiscard]] Result<StorageBytes> encode_outbox_effect(const OutboxEffect& effect);
/// \brief Decodes and validates an effect envelope.
/// \param encoded Versioned binary envelope; borrowed only during this call.
/// \return Validated effect or a malformed/unsupported envelope error.
[[nodiscard]] Result<OutboxEffect> decode_outbox_effect(std::string_view encoded);

class IAppliedTargetRepository;

/// \brief Storage contract for idempotent effects and fenced delivery lifecycle.
/// \details Mutations borrow a thread-affine transaction and never commit it.
/// Returned revisions are candidates until commit reports Committed. A worker
/// must confirm both claim and admission commits before calling its transport.
/// Unknown requires a new authoritative read; it never authorizes dispatch.
/// Every delivery mutation returning an error rolls the transaction back.
/// Allocation exceptions propagate; the owner must roll back during unwinding.
/// \thread_safety Instances are immutable and may be shared; transactions may not.
class IOutboxRepository {
public:
    virtual ~IOutboxRepository() = default;
    /// Loads one effect from a transaction snapshot.
    /// \param id Stable effect identity.
    /// \param transaction Caller-owned transaction.
    /// \return Effect or empty when absent.
    [[nodiscard]] virtual Result<std::optional<OutboxEffect>> load(
        const OutboxEffectId& id, ITransaction& transaction) const = 0;
    /// \brief Creates an effect or verifies byte-identical replay.
    /// \param effect Candidate effect.
    /// \param transaction Caller-owned read-write transaction.
    /// \return Existing or newly staged effect.
    /// Return existing/equivalent effect on idempotent replay; conflict for a
    /// different payload occupying the same logical slot.
    [[nodiscard]] virtual Result<OutboxEffect> create_or_validate(
        const OutboxEffect& effect, ITransaction& transaction) const = 0;

    /// \brief Claims a bounded page of eligible effects with a fencing token.
    /// \param limit Maximum number of effects to claim.
    /// \param now Authoritative current time.
    /// \param lease_duration Positive lease duration.
    /// \param continuation Opaque cursor returned by a previous scan.
    /// \param transaction Caller-owned read-write transaction.
    /// \return Claimed effects and bounded-scan progress.
    /// \note An expired pre-dispatch claim may be reclaimed. An expired
    /// `DispatchAdmitted` row is conservatively changed to `UnknownOutcome`.
    /// Exhausted or obsolete unadmitted work becomes Failed without dispatch.
    [[nodiscard]] virtual Result<OutboxEffectPage> claim_outbox_effects(
        std::size_t limit, ClockTime now, std::chrono::milliseconds lease_duration,
        std::string_view continuation, ITransaction& transaction) const = 0;

    /// \brief Renews a live delivery lease without changing its token.
    /// \param id Canonical effect identity, including its LT1 target.
    /// \param token Current delivery claim token.
    /// \param expected_revision Transaction-visible effect revision.
    /// \param now Authoritative time used to reject expired claims.
    /// \param lease_until New future expiry in the storage clock domain.
    /// \param transaction Borrowed read-write transaction.
    /// \return Candidate renewed effect, or a stale-claim/storage error.
    [[nodiscard]] virtual Result<OutboxEffect> renew_outbox_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ClockTime lease_until, ITransaction& transaction) const = 0;

    /// \brief Stages the conservative boundary immediately before external dispatch.
    /// \param id Canonical effect identity, including its LT1 target.
    /// \param token Live delivery claim token.
    /// \param expected_revision Transaction-visible effect revision.
    /// \param now Authoritative time used to validate the live lease.
    /// \param transaction Borrowed read-write transaction; the owner must confirm commit.
    /// \return Candidate admitted effect, or a stale-claim/target-conflict error.
    /// \details Atomically reserves the target against other admitted effects.
    /// An unresolved UnknownOutcome keeps that reservation even after lease expiry.
    [[nodiscard]] virtual Result<OutboxEffect> mark_dispatch_admitted(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const = 0;

    /// \brief Records confirmed success and atomically advances applied target state.
    /// \param id Canonical identity of the admitted effect.
    /// \param token Live delivery claim token.
    /// \param expected_revision Observed effect revision; re-read after a duplicate completion.
    /// \param now Authoritative time used to reject stale callbacks.
    /// \param applied Repository used to persist authoritative target evidence.
    /// \param physical_identity Optional transport identity confirmed by the adapter.
    /// \param transaction Borrowed read-write transaction containing both writes.
    /// \return Candidate succeeded effect; a same-token Succeeded row is unchanged.
    /// \warning Errors roll back the borrowed transaction; callers must not commit it.
    [[nodiscard]] virtual Result<OutboxEffect> acknowledge_success(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, std::optional<std::string> physical_identity,
        IAppliedTargetRepository& applied, ITransaction& transaction) const = 0;

    /// \brief Resolves an UnknownOutcome using authoritative typed evidence.
    /// \param evidence Exact effect, claim, row revision, and LT1 target evidence.
    /// \param now Authoritative time recorded on the terminal transition.
    /// \param applied Repository used to persist confirmed target evidence.
    /// \param transaction Borrowed read-write transaction containing both writes.
    /// \return Candidate Succeeded effect, or a stale/conflict error.
    /// \details This path never requires a live lease and never calls transport.
    /// It accepts only an UnknownOutcome whose DT1 owner still matches the saved
    /// effect identity and token. Newer or conflicting applied evidence wins.
    [[nodiscard]] virtual Result<OutboxEffect> resolve_unknown_success(
        const UnknownSuccessEvidence& evidence, ClockTime now,
        IAppliedTargetRepository& applied, ITransaction& transaction) const = 0;

    /// \brief Records proven non-acceptance with bounded retry time and attempt budget.
    /// \param id Canonical identity of the claimed or admitted effect.
    /// \param token Live delivery claim token.
    /// \param expected_revision Observed effect revision.
    /// \param now Authoritative current time.
    /// \param next_eligible_at Adapter-selected retry time, not earlier than now.
    /// \param transaction Borrowed read-write transaction.
    /// \return Candidate RetryableFailure, or Failed when its attempt budget is exhausted.
    /// \pre The adapter proved the external operation was not accepted; response loss is not proof.
    [[nodiscard]] virtual Result<OutboxEffect> record_retryable_failure(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ClockTime next_eligible_at, ITransaction& transaction) const = 0;

    /// \brief Records an ambiguous external outcome without making it eligible again.
    /// \param id Canonical identity of the admitted effect.
    /// \param token Live delivery claim token.
    /// \param expected_revision Observed effect revision.
    /// \param now Authoritative current time.
    /// \param transaction Borrowed read-write transaction.
    /// \return Candidate UnknownOutcome retaining target ownership, or a stale-claim error.
    [[nodiscard]] virtual Result<OutboxEffect> record_unknown_outcome(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const = 0;

    /// \brief Records a terminal failure under the current fencing token.
    /// \param id Canonical effect identity.
    /// \param token Current token; non-unknown work also requires a live lease.
    /// \param expected_revision Observed effect revision.
    /// \param now Authoritative current time.
    /// \param transaction Borrowed read-write transaction.
    /// \return Candidate Failed effect without advancing applied state.
    /// \pre For admitted/unknown work the caller has selected an explicit terminal
    /// resolution policy; this does not prove that the remote effect never occurred.
    [[nodiscard]] virtual Result<OutboxEffect> record_failed(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const = 0;

    /// \brief Re-queues an unknown effect only with an explicit safe-repeat proof.
    /// \param id Canonical identity of the unknown effect.
    /// \param token Token of the unresolved attempt.
    /// \param expected_revision Observed unknown effect revision.
    /// \param now Authoritative current time.
    /// \param safe_repeat_proof Caller assertion of method-specific idempotency or authoritative
    /// lookup proof; the repository cannot validate external evidence.
    /// \param next_eligible_at Retry time, not earlier than now.
    /// \param transaction Borrowed read-write transaction.
    /// \return Candidate RetryableFailure, Failed if exhausted, or an error when proof is absent.
    [[nodiscard]] virtual Result<OutboxEffect> retry_unknown_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, bool safe_repeat_proof, ClockTime next_eligible_at,
        ITransaction& transaction) const = 0;

    /// \brief Records explicit supersession by a newer effect in the same target.
    /// \param id Canonical identity of the effect being abandoned.
    /// \param token Current token; non-unknown work also requires a live lease.
    /// \param expected_revision Observed effect revision.
    /// \param now Authoritative current time.
    /// \param newer_effect_id Existing successor with a strictly greater desired revision.
    /// \param transaction Borrowed read-write transaction fencing both effect rows.
    /// \return Candidate Superseded effect preserving its immutable payload and successor.
    /// \pre Superseding an admitted/unknown effect is an explicit policy choice accepting
    /// possible late remote execution; it never asserts external cancellation.
    [[nodiscard]] virtual Result<OutboxEffect> supersede_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, const OutboxEffectId& newer_effect_id,
        ITransaction& transaction) const = 0;
};

/// \brief In-memory reference implementation backed by the generic storage API.
class OutboxRepository final : public IOutboxRepository {
public:
    /// \brief Creates a reference repository with bounded scan controls.
    /// \param page_size Storage scan page bound, from one to 4096.
    /// \param examination_budget Maximum rows examined by one claim operation.
    /// \param max_attempts Maximum claims before definite unadmitted work becomes Failed.
    explicit OutboxRepository(std::size_t page_size = 64,
                              std::size_t examination_budget = 1024,
                              std::uint32_t max_attempts = 8) noexcept
        : m_page_size(page_size), m_examination_budget(examination_budget),
          m_max_attempts(max_attempts) {}

    /// \copydoc IOutboxRepository::load
    [[nodiscard]] Result<std::optional<OutboxEffect>> load(
        const OutboxEffectId& id, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::create_or_validate
    [[nodiscard]] Result<OutboxEffect> create_or_validate(
        const OutboxEffect& effect, ITransaction& transaction) const override;

    /// \copydoc IOutboxRepository::claim_outbox_effects
    [[nodiscard]] Result<OutboxEffectPage> claim_outbox_effects(
        std::size_t limit, ClockTime now, std::chrono::milliseconds lease_duration,
        std::string_view continuation, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::renew_outbox_effect
    [[nodiscard]] Result<OutboxEffect> renew_outbox_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ClockTime lease_until, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::mark_dispatch_admitted
    [[nodiscard]] Result<OutboxEffect> mark_dispatch_admitted(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::acknowledge_success
    [[nodiscard]] Result<OutboxEffect> acknowledge_success(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, std::optional<std::string> physical_identity,
        IAppliedTargetRepository& applied, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::resolve_unknown_success
    [[nodiscard]] Result<OutboxEffect> resolve_unknown_success(
        const UnknownSuccessEvidence& evidence, ClockTime now,
        IAppliedTargetRepository& applied, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::record_retryable_failure
    [[nodiscard]] Result<OutboxEffect> record_retryable_failure(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ClockTime next_eligible_at, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::record_unknown_outcome
    [[nodiscard]] Result<OutboxEffect> record_unknown_outcome(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::record_failed
    [[nodiscard]] Result<OutboxEffect> record_failed(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::retry_unknown_effect
    [[nodiscard]] Result<OutboxEffect> retry_unknown_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, bool safe_repeat_proof, ClockTime next_eligible_at,
        ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::supersede_effect
    [[nodiscard]] Result<OutboxEffect> supersede_effect(
        const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
        ClockTime now, const OutboxEffectId& newer_effect_id,
        ITransaction& transaction) const override;

private:
    std::size_t m_page_size;
    std::size_t m_examination_budget;
    std::uint32_t m_max_attempts;
};

/// \brief Durable semantic state of a logical target, independent of transport.
struct AppliedTargetState {
    std::string target_identity; ///< Canonical LT1 conversation-scoped target identity.
    std::optional<std::string> physical_identity; ///< Optional transport identity.
    StorageRevision desired_revision{}; ///< Latest desired revision.
    StorageRevision confirmed_applied_revision{}; ///< Last externally confirmed revision.
    std::optional<OutboxEffectId> last_confirmed_effect; ///< Last confirmed effect.
    StorageRevision revision{}; ///< Backend row revision.
};

/// \brief Repository contract for durable applied-target observations.
class IAppliedTargetRepository {
public:
    virtual ~IAppliedTargetRepository() = default;
    /// Loads target state from a transaction snapshot.
    /// \param target_identity Stable target identity.
    /// \param transaction Caller-owned transaction.
    /// \return State or empty when absent.
    [[nodiscard]] virtual Result<std::optional<AppliedTargetState>> load(
        std::string_view target_identity, ITransaction& transaction) const = 0;
    /// Saves target state using expected-revision fencing.
    /// \param state State to persist.
    /// \param expected_revision Observed row revision, or zero to create.
    /// \param transaction Caller-owned read-write transaction.
    /// \return Staged state with candidate revision.
    [[nodiscard]] virtual Result<AppliedTargetState> save(
        const AppliedTargetState& state, StorageRevision expected_revision,
        ITransaction& transaction) const = 0;
};

/// \brief In-memory/reference applied-target repository.
class AppliedTargetRepository final : public IAppliedTargetRepository {
public:
    /// \copydoc IAppliedTargetRepository::load
    [[nodiscard]] Result<std::optional<AppliedTargetState>> load(
        std::string_view target_identity, ITransaction& transaction) const override;
    /// \copydoc IAppliedTargetRepository::save
    [[nodiscard]] Result<AppliedTargetState> save(
        const AppliedTargetState& state, StorageRevision expected_revision,
        ITransaction& transaction) const override;
};

/// \brief Immutable authoritative context required before TX2 can publish.
/// \details A plan with a default or inconsistent context is rejected before
/// any effect or intent transition is staged. `applied_target_revision == 0`
/// is the explicit fence for an absent applied-target row.
struct ReconciliationContext {
    RenderIntentId render_intent_id; ///< Owning render-intent identity.
    ConversationKey conversation; ///< Authoritative conversation identity.
    StorageRevision session_revision{}; ///< Session revision observed during planning.
    StorageRevision desired_revision{}; ///< Desired revision represented by the intent.
    std::uint64_t desired_generation{}; ///< Desired-generation value observed during planning.
    StorageRevision desired_generation_row_revision{}; ///< Generation marker row revision.
    std::string target_identity; ///< Canonical LT1 conversation-scoped logical target.
    EffectKind effect_kind = EffectKind::Replace; ///< Immutable operation kind.
    std::string effect_position; ///< Immutable logical operation slot.
    StorageRevision applied_target_revision{}; ///< Applied-target row revision; zero means absent.
    StorageRevision confirmed_applied_revision{}; ///< Confirmed applied revision observed during planning.
    RenderPolicy render_policy = RenderPolicy::OnChange; ///< Policy used by planning.
    ClaimToken claim_token{}; ///< Original TX2 claim token.
    StorageRevision claim_row_revision{}; ///< Original TX2 claim row revision.
};

/// \brief Input to deterministic transport-neutral planning.
struct ReconciliationInput {
    RenderIntentId render_intent_id; ///< Intent being reconciled.
    std::string target_identity; ///< Canonical LT1 conversation-scoped target identity.
    EffectKind effect_kind = EffectKind::Replace; ///< Planned operation kind.
    std::string effect_position; ///< Stable target slot.
    StorageRevision desired_revision{}; ///< Desired session revision.
    StorageRevision applied_revision{}; ///< Last confirmed applied revision.
    RenderPolicy render_policy = RenderPolicy::OnChange; ///< Render policy.
    std::uint32_t payload_version = 1; ///< Payload schema version.
    std::string canonical_payload; ///< Deterministically rendered payload.
    std::optional<ConversationKey> conversation; ///< Conversation fenced by TX2.
    StorageRevision session_revision{}; ///< Session revision used by planning.
    std::uint64_t desired_generation{}; ///< Desired-generation value used by planning.
    StorageRevision desired_generation_row_revision{}; ///< Generation marker revision.
    StorageRevision applied_target_revision{}; ///< Applied-target row revision observed by planning.
    std::optional<ReconciliationContext> context; ///< Mandatory publishable TX2 context.
};

/// \brief Deterministic result of rendering and comparing one target.
struct ReconciliationPlan {
    bool no_op = false; ///< True when no external effect is required.
    OutboxEffect effect; ///< Effect to publish when not a no-op.
    RenderIntentId render_intent_id; ///< Intent identity bound to this plan.
    std::string target_identity; ///< Canonical LT1 target identity bound to this plan.
    EffectKind effect_kind = EffectKind::Replace; ///< Operation kind bound to this plan.
    std::string effect_position; ///< Logical slot bound to this plan.
    StorageRevision desired_revision{}; ///< Desired revision bound to this plan.
    StorageRevision applied_revision{}; ///< Applied revision observed while planning.
    RenderPolicy render_policy = RenderPolicy::OnChange; ///< Policy used to produce this plan.
    std::optional<ConversationKey> conversation; ///< Conversation bound to this plan.
    StorageRevision session_revision{}; ///< Expected session revision.
    std::uint64_t desired_generation{}; ///< Expected desired-generation value.
    StorageRevision desired_generation_row_revision{}; ///< Expected generation row revision.
    StorageRevision applied_target_revision{}; ///< Expected applied-target row revision.
    ReconciliationContext context; ///< Mandatory immutable TX2 fencing context.
};

/// \brief Builds deterministic plans without holding a storage transaction.
class ReconciliationPlanner final {
public:
    /// Builds a plan without opening a storage transaction.
    /// \param input Rendered desired state and current applied revision.
    /// \return No-op or effect plan.
    [[nodiscard]] Result<ReconciliationPlan> build(const ReconciliationInput& input) const;
};

/// \brief Result of publishing a plan in TX2.
enum class ReconciliationOutcome { Planned, AlreadyApplied, RejectedConflict, Failed, Unknown };

/// \brief Classification returned by authoritative inspection after Unknown.
enum class ReconciliationRecoveryOutcome { Published, NotPublished, Inconclusive };

/// \brief Authoritative records observed while resolving an uncertain TX2 commit.
struct ReconciliationRecovery {
    ReconciliationRecoveryOutcome outcome = ReconciliationRecoveryOutcome::Inconclusive; ///< Recovery classification.
    std::optional<RenderIntentClaim> claim; ///< Current claim side state.
    std::optional<OutboxEffect> effect; ///< Effect with the expected identity, when present.
    std::optional<Error> error; ///< Diagnostic when inspection cannot establish coherence.
};

/// \brief Result of publishing a reconciliation plan.
struct ReconciliationResult {
    ReconciliationOutcome outcome = ReconciliationOutcome::Failed; ///< TX2 outcome.
    std::optional<OutboxEffect> effect; ///< Published effect, when any.
    std::optional<Error> error; ///< Diagnostic for non-success outcomes.
};

/// \brief Atomic TX2 publisher. Rendering is intentionally outside this class.
class AtomicReconciliation final {
public:
    /// Binds the backend and repositories used by TX2.
    /// \param backend Storage backend.
    /// \param claims Render-intent claim repository.
    /// \param outbox Outbox repository.
    AtomicReconciliation(IStorageBackend& backend,
                         const RenderIntentClaimRepository& claims,
                         const IOutboxRepository& outbox) noexcept
        : m_backend(backend), m_claims(claims), m_outbox(outbox) {}

    /// \brief Stages TX2 in a caller-owned transaction without committing it.
    /// \param plan Immutable plan produced outside storage.
    /// \param intent_id Claimed render-intent identity.
    /// \param token Original live claim token; must match `plan.context`.
    /// \param intent_row_revision Original claim row revision; must match `plan.context`.
    /// \param now Current authoritative time.
    /// \param transaction Caller-owned read-write transaction.
    /// \return Staged effect, or empty for a validated no-op.
    /// \warning On any error this method rolls back the borrowed transaction;
    /// the caller must not commit it and must start a new transaction.
    [[nodiscard]] Result<std::optional<OutboxEffect>> stage(
        const ReconciliationPlan& plan, const RenderIntentId& intent_id,
        ClaimToken token, StorageRevision intent_row_revision,
        ClockTime now, ITransaction& transaction) const;

    /// Publishes an effect and intent transition atomically.
    /// \param plan Plan produced outside storage.
    /// \param intent_id Claimed render-intent identity.
    /// \param token Live claim token.
    /// \param intent_row_revision Live claim row revision.
    /// \param now Current authoritative time.
    /// \return Typed TX2 outcome.
    [[nodiscard]] ReconciliationResult publish(
        const ReconciliationPlan& plan, const RenderIntentId& intent_id,
        ClaimToken token, StorageRevision intent_row_revision,
        ClockTime now) const;

    /// \brief Inspects authoritative intent and effect rows after Unknown.
    /// \param plan Original immutable plan and attempt context.
    /// \param intent_id Claimed render-intent identity.
    /// \param now Current authoritative time.
    /// \return Conservative recovery classification; no planning is retried.
    [[nodiscard]] Result<ReconciliationRecovery> inspect_unknown(
        const ReconciliationPlan& plan, const RenderIntentId& intent_id,
        ClockTime now) const;

private:
    IStorageBackend& m_backend;
    const RenderIntentClaimRepository& m_claims;
    const IOutboxRepository& m_outbox;
};

} // namespace imtg

#endif
