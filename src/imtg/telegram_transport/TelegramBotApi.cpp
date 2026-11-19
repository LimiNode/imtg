#include <imtg/telegram_transport.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <utility>

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

std::string form_encode(std::string_view value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) {
        if (is_unreserved(byte)) {
            encoded.push_back(static_cast<char>(byte));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[byte >> 4U]);
            encoded.push_back(hex[byte & 0x0FU]);
        }
    }
    return encoded;
}

std::string form_encode_number(std::int64_t value)
{
    return form_encode(std::to_string(value));
}

bool header_name_equal(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint32_t> retry_after(const HttpResponse& response) noexcept
{
    for (const auto& header : response.headers) {
        if (!header_name_equal(header.first, "Retry-After")) {
            continue;
        }
        std::uint32_t value{};
        const auto* first = header.second.data();
        const auto* last = first + header.second.size();
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec == std::errc{} && parsed.ptr == last
            && value <= 86400U) {
            return value;
        }
    }
    return std::nullopt;
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
        if (text.size() > m_config.max_text_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("sendMessage", "chat_id=" + form_encode_number(chat_id.value)
                        + "&text=" + form_encode(text), cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::edit_message_text(
    ChatId chat_id, std::int64_t message_id, std::string_view text,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (text.size() > m_config.max_text_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("editMessageText", "chat_id=" + form_encode_number(chat_id.value)
                        + "&message_id=" + form_encode_number(message_id)
                        + "&text=" + form_encode(text), cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::answer_callback_query(
    std::string_view callback_query_id,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (callback_query_id.empty()
            || callback_query_id.size() > m_config.max_callback_id_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
        return call("answerCallbackQuery",
                    "callback_query_id=" + form_encode(callback_query_id),
                    cancellation);
    } catch (...) {
        return result(TelegramOutcome::InvalidRequest);
    }
}

TelegramResult TelegramBotApi::call(
    std::string_view method, std::string body,
    const CancellationToken& cancellation) const noexcept
{
    try {
        if (m_bot_token.empty() || body.size() > m_config.max_request_bytes) {
            return result(TelegramOutcome::InvalidRequest);
        }
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

        if (exchange.response.has_value()) {
            const auto& response = *exchange.response;
            TelegramResult classified;
            classified.http_status = response.status_code;
            if (response.body.size() > m_config.max_response_bytes) {
                classified.outcome = TelegramOutcome::UnknownOutcome;
                classified.transport_failure = HttpFailureKind::ResponseTooLarge;
                return classified;
            }
            if (response.status_code == 429) {
                classified.outcome = TelegramOutcome::RateLimited;
                classified.retry_after_seconds = retry_after(response);
                return classified;
            }
            if (response.status_code >= 200 && response.status_code < 300) {
                classified.outcome = TelegramOutcome::Succeeded;
            } else if (response.status_code >= 400 && response.status_code < 500) {
                classified.outcome = TelegramOutcome::ClientError;
            } else if (response.status_code >= 500) {
                classified.outcome = TelegramOutcome::ServerError;
            } else {
                classified.outcome = TelegramOutcome::ClientError;
            }
            return classified;
        }

        if (!exchange.failure.has_value()) {
            return result(TelegramOutcome::TransportFailure);
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
