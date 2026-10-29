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

struct PendingInput {
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const PendingInput& left, const PendingInput& right)
    {
        return left.version == right.version && left.name == right.name;
    }
};

struct ScreenIdentity {
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const ScreenIdentity& left, const ScreenIdentity& right)
    {
        return left.version == right.version && left.name == right.name;
    }
};

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

enum class MetadataPatchMode { Keep, Clear, Set };

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

[[nodiscard]] Result<StorageBytes> encode_session_record(const SessionRecord& record);
[[nodiscard]] Result<SessionRecord> decode_session_record(std::string_view encoded);

class ISessionRepository {
public:
    virtual ~ISessionRepository() = default;
    [[nodiscard]] virtual Result<std::optional<SessionRecord>> load(
        const ConversationKey&, ITransaction&) const = 0;
    [[nodiscard]] virtual Result<SessionRecord> create(
        SessionRecord, ITransaction&) const = 0;
    [[nodiscard]] virtual Result<SessionRecord> save(
        SessionRecord, StorageRevision expected_revision, ITransaction&) const = 0;
    [[nodiscard]] virtual Result<void> erase(
        const ConversationKey&, StorageRevision expected_revision, ITransaction&) const = 0;
};

class SessionRepository final : public ISessionRepository {
public:
    explicit SessionRepository(std::uint32_t schema_version = 1,
                               std::size_t max_record_size = 64 * 1024) noexcept;

    [[nodiscard]] Result<std::optional<SessionRecord>> load(
        const ConversationKey&, ITransaction&) const override;
    [[nodiscard]] Result<SessionRecord> create(
        SessionRecord, ITransaction&) const override;
    [[nodiscard]] Result<SessionRecord> save(
        SessionRecord, StorageRevision expected_revision, ITransaction&) const override;
    [[nodiscard]] Result<void> erase(
        const ConversationKey&, StorageRevision expected_revision, ITransaction&) const override;

private:
    [[nodiscard]] Result<void> validate(const SessionRecord&) const;

    std::uint32_t m_schema_version;
    std::size_t m_max_record_size;
};

struct UpdateIdentity {
    BotId bot_id;
    std::uint64_t update_id{};

    [[nodiscard]] std::string encode() const;
    friend bool operator==(const UpdateIdentity& left, const UpdateIdentity& right) noexcept
    {
        return left.bot_id == right.bot_id && left.update_id == right.update_id;
    }
};

class IProcessedUpdateStore {
public:
    virtual ~IProcessedUpdateStore() = default;
    [[nodiscard]] virtual Result<bool> record_if_new(
        const UpdateIdentity&, ITransaction&) const = 0;
};

class ProcessedUpdateRepository final : public IProcessedUpdateStore {
public:
    [[nodiscard]] Result<bool> record_if_new(
        const UpdateIdentity&, ITransaction&) const override;
};

struct RenderIntentId {
    std::string value;

    friend bool operator==(const RenderIntentId& left, const RenderIntentId& right)
    {
        return left.value == right.value;
    }
};

struct RenderIntent {
    RenderIntentId id;
    ConversationKey conversation;
    StorageRevision session_revision{};
    std::string metadata;
    std::optional<UpdateIdentity> origin;
};

class IRenderIntentStore {
public:
    virtual ~IRenderIntentStore() = default;
    [[nodiscard]] virtual Result<RenderIntentId> append(
        const RenderIntent&, ITransaction&) const = 0;
    [[nodiscard]] virtual Result<std::optional<RenderIntent>> load(
        const RenderIntentId&, ITransaction&) const = 0;
};

class RenderIntentRepository final : public IRenderIntentStore {
public:
    [[nodiscard]] Result<RenderIntentId> append(
        const RenderIntent&, ITransaction&) const override;
    [[nodiscard]] Result<std::optional<RenderIntent>> load(
        const RenderIntentId&, ITransaction&) const override;

    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey&, StorageRevision session_revision);
    [[nodiscard]] static RenderIntentId make_id(
        const ConversationKey&, StorageRevision session_revision, const UpdateIdentity& origin);
};

enum class UpdateProcessOutcome {
    Committed,
    Duplicate,
    RejectedConflict,
    Failed,
    Unknown,
};

struct UpdateProcessResult {
    UpdateProcessOutcome outcome;
    std::optional<Error> error;
    std::optional<SessionRecord> session;
    std::optional<RenderIntentId> render_intent;
};

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

/// Atomically records an update, evaluates a transition, saves the session and
/// optionally appends a minimal render intent in one borrowed transaction.
class AtomicUpdateProcessor final {
public:
    AtomicUpdateProcessor(IStorageBackend&, const ISessionRepository&,
                          const IProcessedUpdateStore&, const IRenderIntentStore&);

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
