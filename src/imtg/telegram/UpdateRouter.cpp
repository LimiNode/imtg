#include <imtg/telegram.hpp>

#include <logit.hpp>

#include <exception>
#include <limits>
#include <utility>

namespace imtg::telegram {

namespace {

constexpr std::size_t max_command_length = 32;
constexpr std::size_t max_username_length = 32;

struct ParsedCommand {
    std::string command;
    std::string target_username;
    bool valid = false;
};

bool valid_username_part(std::string_view value) noexcept
{
    if (value.empty() || value.size() > max_username_length) return false;
    for (const auto character : value) {
        const auto ascii = static_cast<unsigned char>(character);
        if (!((ascii >= 'a' && ascii <= 'z') || (ascii >= 'A' && ascii <= 'Z')
              || (ascii >= '0' && ascii <= '9') || ascii == '_')) {
            return false;
        }
    }
    return true;
}

bool valid_command(std::string_view value) noexcept
{
    if (value.empty() || value.size() > max_command_length) return false;
    for (const auto character : value) {
        const auto ascii = static_cast<unsigned char>(character);
        if (!((ascii >= 'a' && ascii <= 'z') || (ascii >= '0' && ascii <= '9')
              || ascii == '_')) {
            return false;
        }
    }
    return true;
}

ParsedCommand parse_command(std::string_view text)
{
    if (text.empty() || text.front() != '/') {
        return {};
    }
    const auto end = text.find_first_of(" \t\r\n");
    const auto token = text.substr(1, end == std::string_view::npos ? text.size() - 1 : end - 1);
    const auto mention = token.find('@');
    const auto command = mention == std::string_view::npos
        ? token : token.substr(0, mention);
    const auto target = mention == std::string_view::npos
        ? std::string_view{} : token.substr(mention + 1);
    if (mention != std::string_view::npos
        && token.find('@', mention + 1) != std::string_view::npos) {
        return {};
    }
    if (!valid_command(command)
        || (mention != std::string_view::npos && !valid_username_part(target))) {
        return {};
    }
    return ParsedCommand{std::string(command), std::string(target), true};
}

bool is_bot_command(std::string_view text) noexcept
{
    return !text.empty() && text.front() == '/';
}

RouterConfig normalized_config(RouterConfig config) noexcept
{
    if (config.max_update_bytes == 0) config.max_update_bytes = RouterConfig::default_max_update_bytes;
    if (config.max_text_bytes == 0) config.max_text_bytes = RouterConfig::default_max_text_bytes;
    if (config.max_callback_bytes == 0) config.max_callback_bytes = RouterConfig::default_max_callback_bytes;
    if (config.max_seen_updates == 0) config.max_seen_updates = RouterConfig::default_max_seen_updates;
    return config;
}

bool same_username(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](unsigned char value) {
            return value >= 'A' && value <= 'Z'
                ? static_cast<unsigned char>(value - 'A' + 'a') : value;
        };
        if (lower(static_cast<unsigned char>(left[index]))
            != lower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::size_t payload_size(const Update& update)
{
    return std::visit([](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, TextMessage>) {
            return payload.text.size();
        } else {
            if (payload.id.size() > std::numeric_limits<std::size_t>::max()
                                    - payload.data.size()) {
                return std::numeric_limits<std::size_t>::max();
            }
            return payload.id.size() + payload.data.size();
        }
    }, update.payload);
}

} // namespace

UpdateKind Update::kind() const noexcept
{
    if (std::holds_alternative<CallbackQuery>(payload)) return UpdateKind::CallbackQuery;
    return is_bot_command(std::get<TextMessage>(payload).text)
        ? UpdateKind::BotCommand : UpdateKind::TextMessage;
}

bool operator==(const UpdateIdentity& left, const UpdateIdentity& right) noexcept
{
    return left.bot_id == right.bot_id && left.update_id == right.update_id;
}

std::size_t UpdateRouter::UpdateIdentityHash::operator()(
    const UpdateIdentity& value) const noexcept
{
    std::size_t seed = std::hash<BotId>{}(value.bot_id);
    seed ^= std::hash<std::uint64_t>{}(value.update_id) + static_cast<std::size_t>(0x9e3779b9U)
        + (seed << 6) + (seed >> 2);
    return seed;
}

struct UpdateRouter::DeduplicationState {
    explicit DeduplicationState(std::size_t max_seen)
        : max_seen_updates(max_seen)
    {
        seen_order.reserve(max_seen_updates);
    }

    mutable std::mutex mutex;
    // One node is allocated at admission and retained through completion.
    std::unordered_set<UpdateIdentity, UpdateIdentityHash> reserved_updates;
    std::vector<UpdateIdentity> seen_order;
    std::size_t next_completed = 0;
    const std::size_t max_seen_updates;
};

UpdateRouter::UpdateRouter(ReferenceRuntime& runtime,
                           RouterConfig config,
                           OutcomeSink outcome_sink)
    : m_runtime(runtime), m_config(normalized_config(config)),
      m_outcome_sink(std::move(outcome_sink)),
      m_deduplication(std::make_shared<DeduplicationState>(m_config.max_seen_updates))
{
}

Result<std::size_t> UpdateRouter::add_route(Route route)
{
    if (!route.handler) {
        return Error{ErrorCode::InvalidArgument, "telegram route handler is empty"};
    }
    if (route.kind == UpdateKind::BotCommand
        && !valid_command(route.command)) {
        return Error{ErrorCode::InvalidArgument, "bot-command route needs a command"};
    }
    if (route.kind != UpdateKind::BotCommand && !route.command.empty()) {
        return Error{ErrorCode::InvalidArgument, "command filter is only valid for bot commands"};
    }
    if (route.kind != UpdateKind::CallbackQuery && !route.callback_data.empty()) {
        return Error{ErrorCode::InvalidArgument,
                     "callback-data filter is only valid for callback queries"};
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_accepting) {
        return Error{ErrorCode::InvalidState, "telegram router is closed"};
    }
    if (m_started) {
        return Error{ErrorCode::InvalidState, "routes cannot change after intake begins"};
    }
    const auto id = m_routes.size();
    m_routes.push_back(std::make_shared<const Route>(std::move(route)));
    return id;
}

Result<void> UpdateRouter::add_middleware(Middleware middleware)
{
    if (!middleware) {
        return Error{ErrorCode::InvalidArgument, "telegram middleware is empty"};
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_accepting) {
        return Error{ErrorCode::InvalidState, "telegram router is closed"};
    }
    if (m_started) {
        return Error{ErrorCode::InvalidState, "middleware cannot change after intake begins"};
    }
    auto next = std::make_shared<std::vector<Middleware>>(*m_middleware);
    next->push_back(std::move(middleware));
    m_middleware = std::move(next);
    return Result<void>();
}

Result<void> UpdateRouter::validate(const Update& update) const
{
    if (update.bot_id.value == 0 || update.update_id == 0) {
        return Error{ErrorCode::InvalidArgument, "telegram update identity is invalid"};
    }
    if (payload_size(update) > m_config.max_update_bytes) {
        return Error{ErrorCode::InvalidArgument, "telegram update exceeds configured limit"};
    }
    return std::visit([this](const auto& payload) -> Result<void> {
        using Payload = std::decay_t<decltype(payload)>;
        if (payload.chat_id.value == 0 || payload.user_id.value == 0
            || payload.message_id.value <= 0
            || !(payload.message_id.chat_id == payload.chat_id)) {
            return Error{ErrorCode::InvalidArgument, "telegram update identity is incomplete"};
        }
        if constexpr (std::is_same_v<Payload, TextMessage>) {
            if (payload.text.size() > m_config.max_text_bytes) {
                return Error{ErrorCode::InvalidArgument, "telegram text exceeds configured limit"};
            }
            if (is_bot_command(payload.text) && !parse_command(payload.text).valid) {
                return Error{ErrorCode::InvalidArgument, "telegram bot command is malformed"};
            }
        } else {
            if (payload.id.empty() || payload.data.empty()
                || payload.data.size() > m_config.max_callback_bytes) {
                return Error{ErrorCode::InvalidArgument, "telegram callback is incomplete or oversized"};
            }
        }
        return Result<void>();
    }, update.payload);
}

Result<ConversationKey> UpdateRouter::make_conversation_key(const Update& update) const
{
    return std::visit([this, &update](const auto& payload) {
        SessionScope scope = Chat{};
        if (m_config.conversation_scope == ConversationScope::UserInChat) {
            scope = UserInChat{payload.user_id};
        }
        return Result<ConversationKey>(ConversationKey{update.bot_id, payload.chat_id, scope});
    }, update.payload);
}

bool UpdateRouter::matches(const Route& route, const Update& update) const
{
    if (route.kind == UpdateKind::CallbackQuery) {
        if (!std::holds_alternative<CallbackQuery>(update.payload)) return false;
        const auto& callback = std::get<CallbackQuery>(update.payload);
        return route.callback_data.empty() || route.callback_data == callback.data;
    }
    if (!std::holds_alternative<TextMessage>(update.payload)) return false;
    const auto& message = std::get<TextMessage>(update.payload);
    if (route.kind == UpdateKind::TextMessage) {
        return !is_bot_command(message.text);
    }
    const auto command = parse_command(message.text);
    if (!command.valid) return false;
    if (!command.target_username.empty()
        && (m_config.bot_username.empty()
            || !same_username(command.target_username, m_config.bot_username))) {
        return false;
    }
    return command.command == route.command;
}

Result<void> validate_binding(const UpdateBinding& binding, const UpdateContext& context)
{
    if (binding.bot_id && !(context.update.bot_id == *binding.bot_id)) {
        return Error{ErrorCode::InvalidArgument, "telegram route bot binding mismatched"};
    }
    return std::visit([&binding](const auto& payload) -> Result<void> {
        if (binding.chat_id && !(payload.chat_id == *binding.chat_id)) {
            return Error{ErrorCode::InvalidArgument, "telegram route chat binding mismatched"};
        }
        if (binding.user_id && !(payload.user_id == *binding.user_id)) {
            return Error{ErrorCode::InvalidArgument, "telegram route user binding mismatched"};
        }
        if (binding.message_id
            && (!(payload.message_id.chat_id == binding.message_id->chat_id)
                || payload.message_id.value != binding.message_id->value)) {
            return Error{ErrorCode::InvalidArgument, "telegram route message binding mismatched"};
        }
        return Result<void>();
    }, context.update.payload);
}

UpdateRouter::RoutePtr UpdateRouter::select_route(const Update& update) const
{
    RoutePtr wildcard_callback;
    for (const auto& route : m_routes) {
        if (!matches(*route, update)) continue;
        if (route->kind == UpdateKind::CallbackQuery && route->callback_data.empty()) {
            if (!wildcard_callback) wildcard_callback = route;
            continue;
        }
        return route;
    }
    return wildcard_callback;
}

void UpdateRouter::forget(const UpdateIdentity& identity)
{
    std::lock_guard<std::mutex> lock(m_deduplication->mutex);
    m_deduplication->reserved_updates.erase(identity);
}

void UpdateRouter::complete(const std::shared_ptr<DeduplicationState>& state,
                            const UpdateIdentity& identity) noexcept
{
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->reserved_updates.find(identity) == state->reserved_updates.end()) return;
    if (state->seen_order.size() < state->max_seen_updates) {
        state->seen_order.push_back(identity);
    } else {
        state->reserved_updates.erase(state->seen_order[state->next_completed]);
        state->seen_order[state->next_completed] = identity;
        state->next_completed = (state->next_completed + 1) % state->max_seen_updates;
    }
}

UpdateOutcome UpdateRouter::receive(Update update)
{
    const auto outcome = [&]() -> UpdateOutcome {
        const auto identity = UpdateIdentity{update.bot_id, update.update_id};
        const auto deduplication = m_deduplication;
        if (!validate(update)) return UpdateOutcome::Invalid;

        RoutePtr route;
        MiddlewareList middleware;
        ConversationKey key;
        bool reserved = false;
        try {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_accepting) return UpdateOutcome::Closed;
                m_started = true;
                std::lock_guard<std::mutex> deduplication_lock(deduplication->mutex);
                if (deduplication->reserved_updates.find(identity)
                        != deduplication->reserved_updates.end()) {
                    return UpdateOutcome::Duplicate;
                }
                route = select_route(update);
                if (!route) return UpdateOutcome::Rejected;
                auto conversation = make_conversation_key(update);
                if (!conversation) return UpdateOutcome::Invalid;
                key = conversation.value();
                const auto insertion = deduplication->reserved_updates.insert(identity);
                if (!insertion.second) return UpdateOutcome::Duplicate;
                reserved = true;
                // The in-flight reservation is the admission record. Completed
                // history is updated only after the callback returns.
                middleware = m_middleware;
            }

            UpdateContext context{std::move(update), key, route->mode};
            const auto sink = m_outcome_sink;
            const auto accepted = m_runtime.submit_callback(
                key,
                [deduplication, identity, route, middleware, context = std::move(context), sink]() mutable
                    -> Result<void> {
                    const auto report = [&sink, &context](UpdateOutcome outcome, const Error& error) {
                        if (!sink) return;
                        try { sink(outcome, context, error); } catch (...) { }
                    };
                    try {
                        for (const auto& policy : *middleware) {
                            const auto result = policy(context);
                            if (!result) {
                                report(UpdateOutcome::Rejected, result.error());
                                complete(deduplication, identity);
                                return result;
                            }
                        }
                        const auto binding = validate_binding(route->binding, context);
                        if (!binding) {
                            report(UpdateOutcome::Unauthorized, binding.error());
                            complete(deduplication, identity);
                            return binding;
                        }
                        if (route->authorize) {
                            const auto result = route->authorize(context);
                            if (!result) {
                                report(UpdateOutcome::Unauthorized, result.error());
                                complete(deduplication, identity);
                                return result;
                            }
                        }
                        if (route->check_stale) {
                            const auto result = route->check_stale(context);
                            if (!result) {
                                report(UpdateOutcome::Stale, result.error());
                                complete(deduplication, identity);
                                return result;
                            }
                        }
                        const auto result = route->handler(context);
                        if (!result) report(UpdateOutcome::HandlerFailed, result.error());
                        complete(deduplication, identity);
                        return result;
                    } catch (const std::exception& exception) {
                        const Error error{ErrorCode::HandlerUnavailable, exception.what()};
                        report(UpdateOutcome::HandlerFailed, error);
                        complete(deduplication, identity);
                        return error;
                    } catch (...) {
                        const Error error{ErrorCode::HandlerUnavailable,
                                          "telegram route callback threw an unknown exception"};
                        report(UpdateOutcome::HandlerFailed, error);
                        complete(deduplication, identity);
                        return error;
                    }
                });
            if (accepted) return UpdateOutcome::Accepted;
            forget(identity);
            return accepted.error().code == ErrorCode::QueueFull
                ? UpdateOutcome::QueueFull
                : accepted.error().code == ErrorCode::InvalidState
                    ? UpdateOutcome::Closed : UpdateOutcome::Rejected;
        } catch (...) {
            // Admission is fail-closed: a failed allocation or callback wrapper
            // construction must not consume the deduplication slot permanently.
            if (reserved) forget(identity);
            return UpdateOutcome::Rejected;
        }
    }();
    // Do not pass update payloads, identities or error messages to the logger.
    try {
        if (outcome == UpdateOutcome::Accepted) {
            LOGIT_INFO_TAG("telegram.update.admission",
                LOGIT_TAG("outcome", static_cast<int>(outcome)));
        } else {
            LOGIT_WARN_TAG("telegram.update.admission",
                LOGIT_TAG("outcome", static_cast<int>(outcome)));
        }
    } catch (...) { }
    return outcome;
}

void UpdateRouter::close() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_accepting) return;
        m_accepting = false;
    }
    try { LOGIT_INFO("telegram.router.closed"); } catch (...) { }
}

} // namespace imtg::telegram
