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
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const PendingInput& left, const PendingInput& right)
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Versioned identity of the screen represented by a session.
struct ScreenIdentity {
    std::string name;
    std::uint32_t version{};

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
    ConversationKey conversation;
    std::uint32_t schema_version = 1;
    FsmStateId state;
    ScreenIdentity screen;
    StorageRevision revision{};
    std::optional<PendingInput> pending_input;
    std::optional<ClockTime> deadline;
    ClockTime created_at{};
    ClockTime last_transition_at{};
};

/// \brief Operation applied to one independently mutable session metadata field.
enum class MetadataPatchMode { Keep, Clear, Set };

/// \brief Small Keep/Clear/Set patch used by metadata-only processor updates.
template <typename T>
struct MetadataPatch {
    MetadataPatchMode mode = MetadataPatchMode::Keep;
    std::optional<T> value;

    static MetadataPatch keep() { return {}; }
    static MetadataPatch clear() { return {MetadataPatchMode::Clear, std::nullopt}; }
    static MetadataPatch set(T value) {
        return {MetadataPatchMode::Set, std::optional<T>{std::move(value)}};
    }
};

/// \brief Encodes a bounded, versioned, binary-safe session record.
[[nodiscard]] Result<StorageBytes> encode_session_record(const SessionRecord& record);
/// \brief Decodes and validates a session record without normalizing malformed data.
[[nodiscard]] Result<SessionRecord> decode_session_record(std::string_view encoded);

/// \brief Transaction-borrowing repository contract for session records.
class ISessionRepository {
public:
    virtual ~ISessionRepository() = default;
    /// \brief Loads from the transaction snapshot, including pending writes.
    [[nodiscard]] virtual Result<std::optional<SessionRecord>> load(
        const ConversationKey&, ITransaction&) const = 0;
    /// \brief Creates a revision-zero logical session with CAS-safe publication.
    [[nodiscard]] virtual Result<SessionRecord> create(
        SessionRecord, ITransaction&) const = 0;
    /// \brief Saves using the caller's expected revision.
    [[nodiscard]] virtual Result<SessionRecord> save(
        SessionRecord, StorageRevision expected_revision, ITransaction&) const = 0;
    /// \brief Erases using the caller's expected revision.
    [[nodiscard]] virtual Result<void> erase(
        const ConversationKey&, StorageRevision expected_revision, ITransaction&) const = 0;
};

/// \brief In-memory/backend-neutral implementation of the session repository.
class SessionRepository final : public ISessionRepository {
public:
    /// \brief Creates a repository with a schema and encoded-record size limit.
    explicit SessionRepository(std::uint32_t schema_version = 1,
                               std::size_t max_record_size = 64 * 1024) noexcept;

    /// \brief Loads one session from a caller-owned transaction.
    [[nodiscard]] Result<std::optional<SessionRecord>> load(
        const ConversationKey&, ITransaction&) const override;
    /// \brief Creates one session; publication is deferred to transaction commit.
    [[nodiscard]] Result<SessionRecord> create(
        SessionRecord, ITransaction&) const override;
    /// \brief Saves one session with optimistic expected-revision fencing.
    [[nodiscard]] Result<SessionRecord> save(
        SessionRecord, StorageRevision expected_revision, ITransaction&) const override;
    /// \brief Erases one session with optimistic expected-revision fencing.
    [[nodiscard]] Result<void> erase(
        const ConversationKey&, StorageRevision expected_revision, ITransaction&) const override;

private:
    [[nodiscard]] Result<void> validate(const SessionRecord&) const;

    std::uint32_t m_schema_version;
    std::size_t m_max_record_size;
};

/// \brief Stable bot-scoped identity used for processed-update deduplication.
struct UpdateIdentity {
    BotId bot_id;
    std::uint64_t update_id{};

    /// \brief Returns the canonical stable storage key encoding.
    [[nodiscard]] std::string encode() const;
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
    [[nodiscard]] virtual Result<bool> record_if_new(
        const UpdateIdentity&, ITransaction&) const = 0;
};

/// \brief Storage-backed implementation of processed-update deduplication.
class ProcessedUpdateRepository final : public IProcessedUpdateStore {
public:
    /// \brief Transactionally records a bot-scoped update marker.
    [[nodiscard]] Result<bool> record_if_new(
        const UpdateIdentity&, ITransaction&) const override;
};

/// \brief Stable identity of a durable render request.
struct RenderIntentId {
    std::string value;

    friend bool operator==(const RenderIntentId& left, const RenderIntentId& right)
    {
        return left.value == right.value;
    }
};

/// \brief Durable request to reconcile a conversation's rendered UI.
/// \note It records intent only; claiming, delivery and outbox completion are
/// outside M2b. An origin is used for independent Always-policy requests.
struct RenderIntent {
    RenderIntentId id;
    ConversationKey conversation;
    StorageRevision session_revision{};
    std::string metadata;
    std::optional<UpdateIdentity> origin;
};

/// \brief Transactional render-intent repository contract.
class IRenderIntentStore {
public:
    virtual ~IRenderIntentStore() = default;
    [[nodiscard]] virtual Result<RenderIntentId> append(
        const RenderIntent&, ITransaction&) const = 0;
    [[nodiscard]] virtual Result<std::optional<RenderIntent>> load(
        const RenderIntentId&, ITransaction&) const = 0;
};

/// \brief Storage-backed implementation of render-intent persistence.
class RenderIntentRepository final : public IRenderIntentStore {
public:
    /// \brief Appends a validated durable render intent to a transaction.
    [[nodiscard]] Result<RenderIntentId> append(
        const RenderIntent&, ITransaction&) const override;
    /// \brief Loads and validates a render intent from a transaction snapshot.
    [[nodiscard]] Result<std::optional<RenderIntent>> load(
        const RenderIntentId&, ITransaction&) const override;

    /// \brief Builds the coalescing identity for a state-change render.
    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey&, StorageRevision session_revision);
    /// \brief Builds an independent identity for an Always-policy render.
    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey&, StorageRevision session_revision, const UpdateIdentity& origin);
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
    UpdateProcessOutcome outcome;
    std::optional<Error> error;
    std::optional<SessionRecord> session;
    std::optional<RenderIntentId> render_intent;
};

/// \brief Explicit rendering, revision and metadata policy for one update.
/// \note Strict expected revisions require backend compare-only watch support.
struct UpdateProcessOptions {
    bool create_render_intent = true;
    RenderPolicy render_policy = RenderPolicy::OnChange;
    std::optional<StorageRevision> expected_revision;
    bool require_expected_revision = false;
    std::optional<PendingInput> next_pending_input;
    std::optional<ClockTime> next_deadline;
    std::optional<ScreenIdentity> next_screen;
    MetadataPatch<PendingInput> pending_input_patch;
    MetadataPatch<ClockTime> deadline_patch;
    MetadataPatch<ScreenIdentity> screen_patch;
};

/// \brief Atomically records an update, evaluates a transition, saves the session and
/// optionally appends a minimal render intent in one borrowed transaction.
/// \details The processed marker, session mutation and intent are published
/// together. `Unknown` is terminal; callers must inspect authoritative storage
/// in a new transaction rather than retrying this operation.
class AtomicUpdateProcessor final {
public:
    /// \brief Binds repository contracts to one storage backend.
    AtomicUpdateProcessor(IStorageBackend&, const ISessionRepository&,
                          const IProcessedUpdateStore&, const IRenderIntentStore&);

    /// \brief Processes one update under the supplied FSM and storage snapshot.
    /// \param update Bot-scoped update identity for deduplication.
    /// \param conversation Canonical conversation being mutated.
    /// \param fsm Immutable transition graph.
    /// \param event Normal, cancellation or trusted timeout event.
    /// \param now Authoritative UTC/system-clock value for deadline checks.
    /// \param initial_session Required only when the conversation does not yet exist.
    /// \param options Revision, metadata and render policy.
    /// \return A typed outcome; failed/conflicted operations publish no staged records.
    [[nodiscard]] UpdateProcessResult process(
        const UpdateIdentity&, const ConversationKey&, const Fsm&, const FsmEvent&,
        ClockTime now, std::optional<SessionRecord> initial_session = std::nullopt,
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
