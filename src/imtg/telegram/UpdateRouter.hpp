#ifndef IMTG_TELEGRAM_UPDATE_ROUTER_HPP
#define IMTG_TELEGRAM_UPDATE_ROUTER_HPP

/// \file imtg/telegram/UpdateRouter.hpp
/// \brief Typed, transport-neutral Telegram update routing boundary.

#include <imtg/core.hpp>
#include <imtg/runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace imtg::telegram {

/// \brief Telegram update shape supported by the M3a boundary.
enum class UpdateKind {
    TextMessage,
    BotCommand, ///< A slash command normalized from an owned text message.
    CallbackQuery,
};

/// \brief Handler execution mode selected by the application.
enum class HandlerMode {
    Headless,
    Interactive,
};

/// \brief Observable result of update admission or serialized processing.
enum class UpdateOutcome {
    Accepted,
    Duplicate,
    Invalid,
    Rejected,
    Unauthorized,
    Stale,
    QueueFull,
    Closed,
    HandlerFailed,
};

/// \brief Scope used when deriving the transport-neutral conversation key.
enum class ConversationScope {
    Chat,
    UserInChat,
};

/// \brief Stable bot-scoped Telegram update identity.
struct UpdateIdentity {
    BotId bot_id; ///< Bot namespace supplied by the transport boundary.
    std::uint64_t update_id{}; ///< Monotonic identity scoped to `bot_id`.
};

/// \brief Telegram message identity normalized at the adapter boundary.
struct MessageId {
    ChatId chat_id; ///< Chat namespace of the message.
    std::int64_t value{}; ///< Telegram message number.
};

/// \brief Owned text message DTO used by the router.
struct TextMessage {
    ChatId chat_id; ///< Chat containing the message.
    UserId user_id; ///< User that authored the message.
    MessageId message_id; ///< Message identity bound to `chat_id`.
    std::string text; ///< Owned UTF-8 text payload.
};

/// \brief Owned callback query DTO used by the router.
struct CallbackQuery {
    std::string id; ///< Telegram callback-query identity.
    ChatId chat_id; ///< Chat containing the source message.
    UserId user_id; ///< User that pressed the callback button.
    MessageId message_id; ///< Source message identity bound to `chat_id`.
    std::string data; ///< Owned callback payload.
};

/// \brief Owned Telegram update; no transport buffer is retained.
struct Update {
    BotId bot_id; ///< Bot namespace that received the update.
    std::uint64_t update_id{}; ///< Bot-scoped update identity.
    std::variant<TextMessage, CallbackQuery> payload; ///< Owned normalized payload.

    /// \brief Classifies the owned payload, including slash commands.
    /// \return Text message, bot command, or callback-query shape represented by this update.
    /// \note Bot commands remain owned `TextMessage` payloads; this method exposes their
    ///       semantic shape without retaining transport-specific parser objects.
    [[nodiscard]] UpdateKind kind() const noexcept;
};

/// \brief Input and admission limits for one router instance.
struct RouterConfig {
    static constexpr std::size_t default_max_update_bytes = 64 * 1024; ///< Default total payload bound.
    static constexpr std::size_t default_max_text_bytes = 32 * 1024; ///< Default text bound.
    static constexpr std::size_t default_max_callback_bytes = 4 * 1024; ///< Default callback bound.
    static constexpr std::size_t default_max_seen_updates = 4096; ///< Default deduplication bound.

    std::size_t max_update_bytes = default_max_update_bytes; ///< Total owned payload bound; zero selects the default.
    std::size_t max_text_bytes = default_max_text_bytes; ///< Text payload bound; zero selects the default.
    std::size_t max_callback_bytes = default_max_callback_bytes; ///< Callback data bound; zero selects the default.
    std::size_t max_seen_updates = default_max_seen_updates; ///< Completed-history bound; zero selects the default.
    std::string bot_username; ///< Optional owned bot username; addressed commands fail closed when empty.
    ConversationScope conversation_scope = ConversationScope::Chat; ///< Derived session scope.
};

/// \brief Owned view delivered to middleware and handlers.
struct UpdateContext {
    Update update; ///< Owned update valid for the callback duration.
    ConversationKey conversation; ///< Canonical serialized conversation key.
    HandlerMode mode = HandlerMode::Headless; ///< Selected application mode.

    /// \brief Returns the update's normalized kind.
    /// \return Update kind.
    [[nodiscard]] UpdateKind kind() const noexcept { return update.kind(); }
};

/// \brief Optional identity constraints enforced inside the serialized route.
struct UpdateBinding {
    std::optional<BotId> bot_id; ///< Expected bot namespace, when constrained.
    std::optional<ChatId> chat_id; ///< Expected chat, when constrained.
    std::optional<UserId> user_id; ///< Expected sender, when constrained.
    std::optional<MessageId> message_id; ///< Expected source message, when constrained.
};

/// \brief Route filter and serialized handler contract.
struct Route {
    UpdateKind kind = UpdateKind::TextMessage; ///< Shape selected by this route.
    std::string command; ///< Normalized command without `/`, for BotCommand only.
    std::string callback_data; ///< Exact callback data, or empty for any callback.
    HandlerMode mode = HandlerMode::Headless; ///< Handler execution mode.
    // These callbacks run asynchronously on the keyed runtime after receive()
    // returns. Captures must own all state needed for the callback lifetime.
    std::function<Result<void>(const UpdateContext&)> authorize; ///< Serialized authorization policy.
    std::function<Result<void>(const UpdateContext&)> check_stale; ///< Serialized freshness policy.
    std::function<Result<void>(const UpdateContext&)> handler; ///< Serialized application callback.
    UpdateBinding binding; ///< Optional bot/chat/user/message authorization binding.
};

/// \brief Deterministic typed Telegram update router over `ReferenceRuntime`.
class UpdateRouter {
public:
    /// \brief Serialized middleware policy callback.
    using Middleware = std::function<Result<void>(const UpdateContext&)>;
    /// \brief Observer for serialized rejection and handler outcomes.
    using OutcomeSink = std::function<void(UpdateOutcome,
                                           const UpdateContext&,
                                           const Error&)>;

    /// \brief Creates a router borrowing an existing keyed runtime.
    /// \param runtime Existing scheduler used for all accepted callbacks.
    /// \param config Validation, deduplication and conversation-scope limits.
    /// \param outcome_sink Observer called on the runtime worker for serialized
    /// rejection/failure; it must tolerate concurrent calls and must not throw.
    /// \pre `runtime` and every object referenced by route, middleware, or sink
    ///     captures outlive this router and the runtime's final `shutdown()`.
    explicit UpdateRouter(ReferenceRuntime& runtime,
                          RouterConfig config = {},
                          OutcomeSink outcome_sink = {});
    UpdateRouter(const UpdateRouter&) = delete;
    UpdateRouter& operator=(const UpdateRouter&) = delete;

    /// \brief Registers a route in deterministic order.
    /// \note Exact callback-data routes take precedence over callback wildcards;
    ///       routes with equal specificity retain registration order.
    /// \param route Complete filter, policy callbacks and handler.
    /// \return Stable route number, or an invalid route error.
    [[nodiscard]] Result<std::size_t> add_route(Route route);

    /// \brief Appends middleware in deterministic registration order.
    /// \param middleware Serialized pre-handler policy callback.
    /// \return Success, or an invalid-state/argument error.
    [[nodiscard]] Result<void> add_middleware(Middleware middleware);

    /// \brief Validates, deduplicates and admits one owned update.
    /// \param update Value-owned update from a transport parser or fixture.
    /// \return Admission outcome; accepted handler failures are reported asynchronously.
    /// \note A valid routed identity is reserved before callback execution and
    ///       remains reserved until the callback completes. The bounded completed
    ///       history therefore cannot evict an in-flight update or admit an
    ///       automatic retry after middleware or handler failure.
    [[nodiscard]] UpdateOutcome receive(Update update);

    /// \brief Stops new intake while accepted callbacks drain in `ReferenceRuntime`.
    void close() noexcept;

private:
    struct UpdateIdentityHash {
        std::size_t operator()(const UpdateIdentity& value) const noexcept;
    };

    using RoutePtr = std::shared_ptr<const Route>;
    using MiddlewareList = std::shared_ptr<const std::vector<Middleware>>;

    [[nodiscard]] Result<void> validate(const Update& update) const;
    [[nodiscard]] Result<ConversationKey> make_conversation_key(const Update& update) const;
    [[nodiscard]] RoutePtr select_route(const Update& update) const;
    [[nodiscard]] bool matches(const Route& route, const Update& update) const;
    void forget(const UpdateIdentity& identity);
    void complete(const UpdateIdentity& identity) noexcept;

    ReferenceRuntime& m_runtime;
    const RouterConfig m_config;
    OutcomeSink m_outcome_sink;
    mutable std::mutex m_mutex;
    std::vector<RoutePtr> m_routes;
    MiddlewareList m_middleware = std::make_shared<const std::vector<Middleware>>();
    std::unordered_set<UpdateIdentity, UpdateIdentityHash> m_in_flight_updates;
    std::unordered_set<UpdateIdentity, UpdateIdentityHash> m_seen_updates;
    std::deque<UpdateIdentity> m_seen_order;
    bool m_started = false;
    bool m_accepting = true;
};

/// \brief Compares bot-scoped update identities.
/// \param left First update identity.
/// \param right Second update identity.
/// \return True when bot and update number match.
[[nodiscard]] bool operator==(const UpdateIdentity& left,
                              const UpdateIdentity& right) noexcept;

} // namespace imtg::telegram

#endif // IMTG_TELEGRAM_UPDATE_ROUTER_HPP
