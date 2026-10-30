#ifndef IMTG_SESSIONS_SESSION_HPP
#define IMTG_SESSIONS_SESSION_HPP

#include "imtg/core/clock.hpp"
#include "imtg/core/identifiers.hpp"
#include "imtg/sessions/fsm.hpp"
#include "imtg/storage/storage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace imtg {

/// \brief Versioned identifier for input expected by the current conversation state.
struct PendingInput {
    std::string name; ///< Stable input name expected by the current state.
    std::uint32_t version{}; ///< Schema version for the expected input.

    /// \brief Compares expected-input identities by name and schema version.
    friend bool operator==(const PendingInput& left, const PendingInput& right)
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Versioned identity of the screen represented by a session.
struct ScreenIdentity {
    std::string name; ///< Stable logical screen name.
    std::uint32_t version{}; ///< Schema version for the screen definition.

    /// \brief Compares screen identities by name and schema version.
    friend bool operator==(const ScreenIdentity& left, const ScreenIdentity& right)
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Persisted conversation session snapshot.
/// \details `revision` is authoritative only when returned by a repository;
/// callers creating a record must leave it zero. Deadlines are UTC/system-clock
/// values and are revalidated by the owner after restart.
struct SessionRecord {
    ConversationKey conversation; ///< Canonical conversation identity.
    std::uint32_t schema_version = 1; ///< Record schema used for encoding.
    FsmStateId state; ///< Current FSM state.
    ScreenIdentity screen; ///< Screen associated with the current state.
    StorageRevision revision{}; ///< Backend revision returned by a repository.
    std::optional<PendingInput> pending_input; ///< Input expected by the current state, if any.
    std::optional<ClockTime> deadline; ///< Optional UTC/system-clock deadline.
    ClockTime created_at{}; ///< Creation timestamp in the authoritative clock domain.
    ClockTime last_transition_at{}; ///< Timestamp of the last accepted transition.
};

/// \brief Operation applied to one independently mutable session metadata field.
enum class MetadataPatchMode { Keep, Clear, Set };

/// \brief Small Keep/Clear/Set patch used by metadata-only processor updates.
template <typename T>
struct MetadataPatch {
    MetadataPatchMode mode = MetadataPatchMode::Keep; ///< Keep, clear, or replace operation.
    std::optional<T> value; ///< Replacement value when `mode` is `Set`.

    /// \brief Leaves the field unchanged.
    /// \return A Keep patch with no replacement value.
    static MetadataPatch keep() { return {}; }
    /// \brief Clears the field when the patch is applied.
    /// \return A Clear patch.
    static MetadataPatch clear() { return {MetadataPatchMode::Clear, std::nullopt}; }
    /// \brief Replaces the field with a value owned by the patch.
    /// \param value New field value.
    /// \return A Set patch containing `value`.
    static MetadataPatch set(T value) {
        return {MetadataPatchMode::Set, std::optional<T>{std::move(value)}};
    }
};

/// \brief Encodes a bounded, versioned, binary-safe session record.
/// \param record Record to encode; its repository revision is not persisted as authority.
/// \return Length-prefixed bytes or a validation/size error.
[[nodiscard]] Result<StorageBytes> encode_session_record(const SessionRecord& record);
/// \brief Decodes and validates a session record without normalizing malformed data.
/// \param encoded Length-prefixed bytes produced by `encode_session_record`.
/// \return The validated record, or a typed decoding error.
[[nodiscard]] Result<SessionRecord> decode_session_record(std::string_view encoded);

/// \brief Transaction-borrowing repository contract for session records.
class ISessionRepository {
public:
    virtual ~ISessionRepository() = default;
    /// \brief Loads from the transaction snapshot, including pending writes.
    /// \param conversation Canonical conversation key.
    /// \param transaction Borrowed transaction owned by the caller.
    /// \return The visible record, if present.
    [[nodiscard]] virtual Result<std::optional<SessionRecord>> load(
        const ConversationKey& conversation, ITransaction& transaction) const = 0;
    /// \brief Creates a revision-zero logical session with CAS-safe publication.
    /// \param record Initial record; its revision must be zero.
    /// \param transaction Borrowed read-write transaction receiving the staged write.
    /// \return The staged record with its candidate revision.
    [[nodiscard]] virtual Result<SessionRecord> create(
        SessionRecord record, ITransaction& transaction) const = 0;
    /// \brief Saves using the caller's expected revision.
    /// \param record Replacement record for the same conversation.
    /// \param expected_revision Revision observed by the caller.
    /// \param transaction Borrowed read-write transaction receiving the staged write.
    /// \return The staged record, or a conflict when the expected revision is stale.
    [[nodiscard]] virtual Result<SessionRecord> save(
        SessionRecord record, StorageRevision expected_revision,
        ITransaction& transaction) const = 0;
    /// \brief Erases using the caller's expected revision.
    /// \param conversation Canonical conversation key.
    /// \param expected_revision Revision observed by the caller.
    /// \param transaction Borrowed read-write transaction receiving the staged erase.
    /// \return Success when the expected revision is still current.
    [[nodiscard]] virtual Result<void> erase(
        const ConversationKey& conversation, StorageRevision expected_revision,
        ITransaction& transaction) const = 0;
};

/// \brief In-memory/backend-neutral implementation of the session repository.
class SessionRepository final : public ISessionRepository {
public:
    /// \brief Creates a repository with a schema and encoded-record size limit.
    /// \param schema_version Version expected in records written by this repository.
    /// \param max_record_size Maximum encoded record size accepted by validation.
    explicit SessionRepository(std::uint32_t schema_version = 1,
                               std::size_t max_record_size = 64 * 1024) noexcept;

    /// \brief Loads one session from a caller-owned transaction.
    /// \param conversation Canonical conversation key.
    /// \param transaction Borrowed transaction used for the snapshot read.
    /// \return The visible record, if present.
    [[nodiscard]] Result<std::optional<SessionRecord>> load(
        const ConversationKey& conversation, ITransaction& transaction) const override;
    /// \brief Creates one session; publication is deferred to transaction commit.
    /// \param record Revision-zero initial record.
    /// \param transaction Borrowed read-write transaction receiving the staged record.
    /// \return The staged record with its candidate revision.
    [[nodiscard]] Result<SessionRecord> create(
        SessionRecord record, ITransaction& transaction) const override;
    /// \brief Saves one session with optimistic expected-revision fencing.
    /// \param record Replacement record for the conversation.
    /// \param expected_revision Revision observed before editing the record.
    /// \param transaction Borrowed read-write transaction receiving the staged record.
    /// \return The staged record, or a conflict when the revision is stale.
    [[nodiscard]] Result<SessionRecord> save(
        SessionRecord record, StorageRevision expected_revision,
        ITransaction& transaction) const override;
    /// \brief Erases one session with optimistic expected-revision fencing.
    /// \param conversation Canonical conversation key.
    /// \param expected_revision Revision observed before erasing the record.
    /// \param transaction Borrowed read-write transaction receiving the staged erase.
    /// \return Success when the expected revision is still current.
    [[nodiscard]] Result<void> erase(
        const ConversationKey& conversation, StorageRevision expected_revision,
        ITransaction& transaction) const override;

private:
    [[nodiscard]] Result<void> validate(const SessionRecord&) const;

    std::uint32_t m_schema_version;
    std::size_t m_max_record_size;
};

/// \brief Stable bot-scoped identity used for processed-update deduplication.
struct UpdateIdentity {
    BotId bot_id; ///< Bot namespace owning the update sequence.
    std::uint64_t update_id{}; ///< Monotonic source update identifier.

    /// \brief Returns the canonical stable storage key encoding.
    /// \return A binary-safe, stable key suitable for storage lookup.
    [[nodiscard]] std::string encode() const;
    /// \brief Compares update identities by bot and source update number.
    friend bool operator==(const UpdateIdentity& left, const UpdateIdentity& right) noexcept
    {
        return left.bot_id == right.bot_id && left.update_id == right.update_id;
    }
};

/// \brief Transactional deduplication store contract.
class IProcessedUpdateStore {
public:
    virtual ~IProcessedUpdateStore() = default;
    /// \brief Records an identity if absent and returns whether it was new.
    /// \param identity Bot-scoped update identity.
    /// \param transaction Borrowed read-write transaction receiving the marker.
    /// \return `true` only for the transaction that first staged the marker.
    [[nodiscard]] virtual Result<bool> record_if_new(
        const UpdateIdentity& identity, ITransaction& transaction) const = 0;
};

/// \brief Storage-backed implementation of processed-update deduplication.
class ProcessedUpdateRepository final : public IProcessedUpdateStore {
public:
    /// \brief Transactionally records a bot-scoped update marker.
    /// \param identity Update identity to record.
    /// \param transaction Borrowed read-write transaction receiving the marker.
    /// \return `true` only when this transaction first staged the marker.
    [[nodiscard]] Result<bool> record_if_new(
        const UpdateIdentity& identity, ITransaction& transaction) const override;
};

/// \brief Stable identity of a durable render request.
struct RenderIntentId {
    std::string value; ///< Canonical deduplication key.

    /// \brief Compares render-intent identities by canonical key.
    friend bool operator==(const RenderIntentId& left, const RenderIntentId& right)
    {
        return left.value == right.value;
    }
};

/// \brief Durable request to reconcile a conversation's rendered UI.
/// \note It records intent only; claiming, delivery and outbox completion are
/// outside M2b. An origin is used for independent Always-policy requests.
struct RenderIntent {
    RenderIntentId id; ///< Stable identity used for coalescing/deduplication.
    ConversationKey conversation; ///< Conversation whose UI must be reconciled.
    StorageRevision session_revision{}; ///< Session revision represented by the intent.
    std::string metadata; ///< Backend-neutral render metadata.
    std::optional<UpdateIdentity> origin; ///< Origin for independent Always-policy requests.
};

/// \brief Transactional render-intent repository contract.
class IRenderIntentStore {
public:
    virtual ~IRenderIntentStore() = default;
    /// \brief Appends one render intent to the transaction.
    /// \param intent Validated intent with a stable deduplication identity.
    /// \param transaction Borrowed read-write transaction receiving the intent.
    /// \return The identity, or a conflict/validation error.
    [[nodiscard]] virtual Result<RenderIntentId> append(
        const RenderIntent& intent, ITransaction& transaction) const = 0;
    /// \brief Loads one intent from the transaction snapshot.
    /// \param id Stable render-intent identity.
    /// \param transaction Borrowed transaction used for the read.
    /// \return The intent when present.
    [[nodiscard]] virtual Result<std::optional<RenderIntent>> load(
        const RenderIntentId& id, ITransaction& transaction) const = 0;
};

/// \brief Storage-backed implementation of render-intent persistence.
class RenderIntentRepository final : public IRenderIntentStore {
public:
    /// \brief Appends a validated durable render intent to a transaction.
    /// \param intent Intent whose identity is used for deduplication.
    /// \param transaction Borrowed read-write transaction receiving the intent.
    /// \return The identity, or a conflict/validation error.
    [[nodiscard]] Result<RenderIntentId> append(
        const RenderIntent& intent, ITransaction& transaction) const override;
    /// \brief Loads and validates a render intent from a transaction snapshot.
    /// \param id Stable render-intent identity.
    /// \param transaction Borrowed transaction used for the snapshot read.
    /// \return The intent when present.
    [[nodiscard]] Result<std::optional<RenderIntent>> load(
        const RenderIntentId& id, ITransaction& transaction) const override;

    /// \brief Builds the coalescing identity for a state-change render.
    /// \param conversation Conversation represented by the render.
    /// \param session_revision Revision of the persisted session rendered.
    /// \return A deterministic coalescing identity.
    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey& conversation, StorageRevision session_revision);
    /// \brief Builds an independent identity for an Always-policy render.
    /// \param conversation Conversation represented by the render.
    /// \param session_revision Revision of the persisted session rendered.
    /// \param origin Update that requested an independent render.
    /// \return A deterministic identity distinct from the coalescing form.
    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey& conversation, StorageRevision session_revision,
        const UpdateIdentity& origin);
};

/// \brief Terminal classification of an atomic update processing attempt.
enum class UpdateProcessOutcome {
    Committed,
    Duplicate,
    RejectedConflict,
    Failed,
    Unknown,
};

/// \brief Result of processing one update, including committed session/intent data.
struct UpdateProcessResult {
    UpdateProcessOutcome outcome; ///< Terminal classification of the attempt.
    std::optional<Error> error; ///< Diagnostic for failed or rejected outcomes.
    std::optional<SessionRecord> session; ///< Staged/committed session when available.
    std::optional<RenderIntentId> render_intent; ///< Published intent identity, if requested.
};

/// \brief Explicit rendering, revision and metadata policy for one update.
/// \note Strict expected revisions require backend compare-only watch support.
struct UpdateProcessOptions {
    bool create_render_intent = true; ///< Whether to stage a render intent on success.
    RenderPolicy render_policy = RenderPolicy::OnChange; ///< Change policy for intent creation.
    std::optional<StorageRevision> expected_revision; ///< Caller-observed session revision.
    bool require_expected_revision = false; ///< Reject processing when no expected revision is supplied.
    std::optional<PendingInput> next_pending_input; ///< Legacy replacement for pending-input metadata.
    std::optional<ClockTime> next_deadline; ///< Legacy replacement for deadline metadata.
    std::optional<ScreenIdentity> next_screen; ///< Legacy replacement for screen metadata.
    MetadataPatch<PendingInput> pending_input_patch; ///< Keep/clear/set pending input.
    MetadataPatch<ClockTime> deadline_patch; ///< Keep/clear/set deadline.
    MetadataPatch<ScreenIdentity> screen_patch; ///< Keep/clear/set screen identity.
};

/// \brief Atomically records an update, evaluates a transition, saves the session and
/// optionally appends a minimal render intent in one borrowed transaction.
/// \details The processed marker, session mutation and intent are published
/// together. `Unknown` is terminal; callers must inspect authoritative storage
/// in a new transaction rather than retrying this operation.
class AtomicUpdateProcessor final {
public:
    /// \brief Binds repository contracts to one storage backend.
    /// \param backend Backend used to create the processing transaction.
    /// \param sessions Session repository sharing that backend.
    /// \param processed Processed-update repository sharing that backend.
    /// \param render_intents Render-intent repository sharing that backend.
    AtomicUpdateProcessor(IStorageBackend& backend, const ISessionRepository& sessions,
                          const IProcessedUpdateStore& processed,
                          const IRenderIntentStore& render_intents);

    /// \brief Processes one update under the supplied FSM and storage snapshot.
    /// \param update Bot-scoped update identity for deduplication.
    /// \param conversation Canonical conversation being mutated.
    /// \param fsm Immutable transition graph.
    /// \param event Normal, cancellation or trusted timeout event.
    /// \param now Authoritative UTC/system-clock value for deadline checks.
    /// \param initial_session Required only when the conversation does not yet exist.
    /// \param options Revision, metadata and render policy.
    /// \return A typed outcome; failed/conflicted operations publish no staged records.
    /// \note The supplied repositories and backend must remain alive for the call.
    [[nodiscard]] UpdateProcessResult process(
        const UpdateIdentity& update, const ConversationKey& conversation, const Fsm& fsm,
        const FsmEvent& event, ClockTime now, std::optional<SessionRecord> initial_session = std::nullopt,
        UpdateProcessOptions options = {}) const;

private:
    IStorageBackend& m_backend;
    const ISessionRepository& m_sessions;
    const IProcessedUpdateStore& m_processed;
    const IRenderIntentStore& m_render_intents;
};

} // namespace imtg

#endif // IMTG_SESSIONS_SESSION_HPP
    /// \brief Creates one session; publication is deferred to the transaction commit.
    /// \brief Saves one session with optimistic expected-revision fencing.
    /// \brief Erases one session with optimistic expected-revision fencing.
    /// \brief Returns the canonical stable storage key encoding.
    /// \brief Records an identity if absent and returns whether it was new.
    /// \brief Loads and validates a render intent from a transaction snapshot.
    /// \brief Builds the coalescing identity for a state-change render.
    /// \brief Builds an independent identity for an Always-policy render.
