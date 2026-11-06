#ifndef IMTG_RECONCILIATION_OUTBOX_HPP
#define IMTG_RECONCILIATION_OUTBOX_HPP

#include "imtg/core/result.hpp"
#include "imtg/sessions/render_intents.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

enum class OutboxEffectState : std::uint8_t {
    Pending,
    ClaimedBeforeDispatch,
    DispatchAdmitted,
    Succeeded,
    RetryableFailure,
    UnknownOutcome,
    Failed,
    Superseded,
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
    std::uint32_t attempt_count{}; ///< Number of delivery attempts.
    StorageRevision revision{}; ///< Backend row revision.
};

/// \brief Encodes an effect in a bounded versioned format.
[[nodiscard]] Result<StorageBytes> encode_outbox_effect(const OutboxEffect& effect);
/// \brief Decodes and validates an effect envelope.
[[nodiscard]] Result<OutboxEffect> decode_outbox_effect(std::string_view encoded);

/// \brief Storage contract for idempotent logical effect insertion.
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
};

/// \brief In-memory reference implementation backed by the generic storage API.
class OutboxRepository final : public IOutboxRepository {
public:
    /// \copydoc IOutboxRepository::load
    [[nodiscard]] Result<std::optional<OutboxEffect>> load(
        const OutboxEffectId& id, ITransaction& transaction) const override;
    /// \copydoc IOutboxRepository::create_or_validate
    [[nodiscard]] Result<OutboxEffect> create_or_validate(
        const OutboxEffect& effect, ITransaction& transaction) const override;
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
