#include <imtg/telegram_transport.hpp>

#include <atomic>
#include <charconv>
#include <limits>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace imtg::telegram_transport {

struct CancellationToken::State {
    std::atomic_bool cancelled{false};
};

CancellationToken::CancellationToken(std::shared_ptr<State> state) noexcept
    : m_state(std::move(state))
{
}

bool CancellationToken::is_cancelled() const noexcept
{
    return m_state != nullptr && m_state->cancelled.load(std::memory_order_acquire);
}

CancellationSource::CancellationSource()
    : m_state(std::make_shared<CancellationToken::State>())
{
}

void CancellationSource::cancel() noexcept
{
    if (m_state != nullptr) {
        m_state->cancelled.store(true, std::memory_order_release);
    }
}

CancellationToken CancellationSource::token() const noexcept
{
    return CancellationToken(m_state);
}

namespace {

TelegramResult result(TelegramOutcome outcome) noexcept
{
    TelegramResult value;
    value.outcome = outcome;
    return value;
}

bool is_unreserved(unsigned char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '-' || value == '_'
        || value == '.' || value == '~';
}

bool append_form_value(std::string& body, std::string_view value, std::size_t limit)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::size_t encoded_size = 0;
    if (body.size() > limit) {
        return false;
    }
    const auto remaining = limit - body.size();
    for (const unsigned char byte : value) {
        const std::size_t added = is_unreserved(byte) ? 1 : 3;
        if (remaining - encoded_size < added) {
            return false;
        }
        encoded_size += added;
    }
    body.reserve(body.size() + encoded_size);
    for (const unsigned char byte : value) {
        if (is_unreserved(byte)) {
            body.push_back(static_cast<char>(byte));
        } else {
            body.push_back('%');
            body.push_back(hex[byte >> 4U]);
            body.push_back(hex[byte & 0x0FU]);
        }
    }
    return true;
}

bool valid_token(std::string_view token) noexcept
{
    const auto colon = token.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 20
        || token.front() == '0' || token.size() - colon - 1 == 0
        || token.size() - colon - 1 > 256) {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        const auto value = token[index];
        if (index < colon) {
            if (value < '0' || value > '9') {
                return false;
            }
        } else if (index > colon && !((value >= 'a' && value <= 'z')
                   || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')
                   || value == '_' || value == '-')) {
            return false;
        }
    }
    return true;
}

bool header_name_equal(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char value) noexcept {
            return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
        };
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

bool retry_after_header(const HttpResponse& response,
                        std::optional<std::uint32_t>& retry_after) noexcept
{
    for (const auto& header : response.headers) {
        if (!header_name_equal(header.first, "Retry-After")) {
            continue;
        }
        std::uint32_t value{};
        const auto* first = header.second.data();
        const auto* last = first + header.second.size();
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last
            || (retry_after.has_value() && *retry_after != value)) {
            return false;
        }
        retry_after = value;
    }
    return true;
}

using Json = nlohmann::json;

struct InvalidEnvelope {};

std::optional<std::int64_t> integer(const Json& value) noexcept
{
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    if (value.is_number_unsigned()
        && value.get<std::uint64_t>() > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int64_t>::max)())) {
        return std::nullopt;
    }
    return value.get<std::int64_t>();
}

TelegramResult parse_response(const HttpResponse& response,
                               std::string_view method,
                               ChatId expected_chat,
                               std::optional<std::int64_t> expected_message)
{
    TelegramResult classified = result(TelegramOutcome::UnknownOutcome);
    classified.http_status = response.status_code;
    classified.transport_failure = HttpFailureKind::MalformedResponse;
    // The library parses JSON syntax. The callback bounds nesting and rejects
    // duplicate keys rather than silently accepting the last contradictory value.
    std::vector<std::unordered_set<std::string>> object_keys;
    const auto envelope = Json::parse(response.body,
        [&object_keys](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 32) {
                throw InvalidEnvelope{};
            }
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::object_end) {
                object_keys.pop_back();
            } else if (event == Json::parse_event_t::key
                       && !object_keys.back().insert(value.get<std::string>()).second) {
                throw InvalidEnvelope{};
            }
            return true;
        });
    if (!envelope.is_object() || !envelope.contains("ok")
        || !envelope["ok"].is_boolean()
        || (envelope.contains("description") && !envelope["description"].is_string())) {
        return classified;
    }
    if (envelope["ok"].get<bool>()) {
        if (response.status_code < 200 || response.status_code >= 300
            || !envelope.contains("result") || envelope.contains("error_code")
            || envelope.contains("parameters")) {
            return classified;
        }
        const auto& payload = envelope["result"];
        if (method == "answerCallbackQuery") {
            if (!payload.is_boolean() || !payload.get<bool>()) {
                return classified;
            }
        } else {
            if (!payload.is_object() || !payload.contains("message_id")
                || !payload.contains("chat") || !payload["chat"].is_object()
                || !payload["chat"].contains("id")) {
                return classified;
            }
            const auto message = integer(payload["message_id"]);
            const auto chat = integer(payload["chat"]["id"]);
            if (!message.has_value() || *message <= 0
                || !chat.has_value() || *chat != expected_chat.value
                || (expected_message.has_value() && *message != *expected_message)) {
                return classified;
            }
            classified.message_id = telegram::MessageId{expected_chat, *message};
        }
        classified.outcome = TelegramOutcome::Succeeded;
        classified.transport_failure.reset();
        return classified;
    }

    if (envelope.contains("result") || !envelope.contains("error_code")
        || !envelope.contains("description") || !envelope["description"].is_string()) {
        return classified;
    }
    const auto error = integer(envelope["error_code"]);
    if (!error.has_value() || *error < 400 || *error > 599
        || response.status_code < 200 || response.status_code >= 600
        || (response.status_code >= 300 && response.status_code != *error)) {
        return classified;
    }
    std::optional<std::uint32_t> delay;
    if (!retry_after_header(response, delay)) {
        return classified;
    }
    if (envelope.contains("parameters")) {
        const auto& parameters = envelope["parameters"];
        if (!parameters.is_object()) {
            return classified;
        }
        if (parameters.contains("retry_after")) {
            const auto retry = integer(parameters["retry_after"]);
            if (!retry.has_value() || *retry < 0
                || static_cast<std::uint64_t>(*retry) > (std::numeric_limits<std::uint32_t>::max)()
                || (delay.has_value() && *delay != static_cast<std::uint32_t>(*retry))) {
                return classified;
            }
            delay = static_cast<std::uint32_t>(*retry);
        }
    }
    if (delay.has_value() && *error != 429) {
        return classified;
    }
    classified.api_error_code = static_cast<std::uint32_t>(*error);
    classified.retry_after_seconds = delay;
    classified.transport_failure.reset();
    classified.outcome = *error == 429 ? TelegramOutcome::RateLimited
        : (*error >= 500 ? TelegramOutcome::ServerError : TelegramOutcome::ClientError);
    return classified;
}

} // namespace

TelegramBotApi::TelegramBotApi(std::string bot_token,
                               IHttpTransport& transport,
                               TelegramBotApiConfig config)
    : m_bot_token(std::move(bot_token))
    , m_transport(transport)
    , m_config(config)
{
    if (m_config.max_request_bytes == 0) {
        m_config.max_request_bytes = 16 * 1024;
    }
    if (m_config.max_response_bytes == 0) {
        m_config.max_response_bytes = 64 * 1024;
    }
    if (m_config.max_text_bytes == 0) {
        m_config.max_text_bytes = 4096;
    }
    if (m_config.max_callback_id_bytes == 0) {
        m_config.max_callback_id_bytes = 256;
    }
    if (m_config.timeout <= std::chrono::milliseconds::zero()) {
        m_config.timeout = std::chrono::milliseconds(10000);
    }
}

TelegramResult TelegramBotApi::send_message(
    ChatId chat_id, std::string_view text,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (!valid_token(m_bot_token) || chat_id.value == 0 || text.empty()
            || text.size() > m_config.max_text_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        std::string body = "chat_id=" + std::to_string(chat_id.value) + "&text=";
        if (!append_form_value(body, text, m_config.max_request_bytes)) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("sendMessage", std::move(body), chat_id, std::nullopt, cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::edit_message_text(
    ChatId chat_id, std::int64_t message_id, std::string_view text,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (!valid_token(m_bot_token) || chat_id.value == 0 || message_id <= 0 || text.empty()
            || text.size() > m_config.max_text_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        std::string body = "chat_id=" + std::to_string(chat_id.value)
            + "&message_id=" + std::to_string(message_id) + "&text=";
        if (!append_form_value(body, text, m_config.max_request_bytes)) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("editMessageText", std::move(body), chat_id, message_id, cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::answer_callback_query(
    std::string_view callback_query_id,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (!valid_token(m_bot_token) || callback_query_id.empty()
            || callback_query_id.size() > m_config.max_callback_id_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        std::string body = "callback_query_id=";
        if (!append_form_value(body, callback_query_id, m_config.max_request_bytes)) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("answerCallbackQuery", std::move(body), ChatId{}, std::nullopt, cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::call(
    std::string_view method, std::string body,
    ChatId expected_chat, std::optional<std::int64_t> expected_message,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (cancellation.is_cancelled()) {
            return result(TelegramOutcome::Cancelled);
        }

        HttpRequest request;
        request.url = "https://api.telegram.org/bot" + m_bot_token + "/"
            + std::string(method);
        request.body = std::move(body);
        request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
        request.timeout = m_config.timeout;
        request.max_response_bytes = m_config.max_response_bytes;
        const auto exchange = m_transport.execute(request, cancellation);

        if (exchange.response.has_value() && !exchange.failure.has_value()
            && exchange.request_may_have_been_sent) {
            const auto& response = *exchange.response;
            TelegramResult classified = result(TelegramOutcome::UnknownOutcome);
            classified.http_status = response.status_code;
            if (response.body.size() > m_config.max_response_bytes) {
                classified.transport_failure = HttpFailureKind::ResponseTooLarge;
                return classified;
            }
            try {
                return parse_response(response, method, expected_chat, expected_message);
            } catch (...) {
                classified.transport_failure = HttpFailureKind::MalformedResponse;
                return classified;
            }
        }

        if (exchange.response.has_value() || !exchange.failure.has_value()) {
            return result(TelegramOutcome::UnknownOutcome);
        }
        TelegramResult failed;
        failed.transport_failure = exchange.failure;
        if (exchange.request_may_have_been_sent
            || *exchange.failure == HttpFailureKind::ResponseTooLarge
            || *exchange.failure == HttpFailureKind::MalformedResponse) {
            failed.outcome = TelegramOutcome::UnknownOutcome;
        } else if (*exchange.failure == HttpFailureKind::Timeout) {
            failed.outcome = TelegramOutcome::Timeout;
        } else if (*exchange.failure == HttpFailureKind::Cancelled) {
            failed.outcome = TelegramOutcome::Cancelled;
        } else {
            failed.outcome = TelegramOutcome::TransportFailure;
        }
        return failed;
    } catch (...) {
        TelegramResult failed;
        failed.outcome = TelegramOutcome::UnknownOutcome;
        failed.transport_failure = HttpFailureKind::MalformedResponse;
        return failed;
    }
}

} // namespace imtg::telegram_transport
