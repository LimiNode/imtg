#ifndef IMTG_TELEGRAM_TRANSPORT_TELEGRAM_BOT_API_HPP
#define IMTG_TELEGRAM_TRANSPORT_TELEGRAM_BOT_API_HPP

/// \file imtg/telegram_transport/TelegramBotApi.hpp
/// \brief Bounded, injectable Telegram Bot API request boundary.

#include <imtg/telegram.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imtg::telegram_transport {

/// \brief Cancellation state shared by a caller and an HTTP adapter.
class CancellationToken {
public:
    /// \brief Creates a token that is not cancelled.
    CancellationToken() noexcept = default;

    /// \brief Reports whether cancellation was requested.
    /// \return True after the associated source has been cancelled.
    [[nodiscard]] bool is_cancelled() const noexcept;

private:
    struct State;
    explicit CancellationToken(std::shared_ptr<State> state) noexcept;
    std::shared_ptr<State> m_state;
    friend class CancellationSource;
};

/// \brief Owns a cancellation flag for one bounded HTTP operation.
class CancellationSource {
public:
    /// \brief Creates an initially active source.
    CancellationSource();

    /// \brief Requests cancellation; repeated calls are harmless.
    void cancel() noexcept;

    /// \brief Returns a token observing this source.
    /// \return Token sharing this source's cancellation state.
    [[nodiscard]] CancellationToken token() const noexcept;

private:
    std::shared_ptr<CancellationToken::State> m_state;
};

/// \brief Failure reported by an injectable HTTP adapter.
enum class HttpFailureKind {
    Network, ///< Connection or protocol failure before a response.
    Timeout, ///< Deadline elapsed before a response.
    Cancelled, ///< Caller or adapter cancelled the exchange.
    RequestTooLarge, ///< Adapter rejected the request body bound.
    ResponseTooLarge, ///< Adapter rejected the response body bound.
    MalformedResponse, ///< Response could not be decoded safely.
};

/// \brief Bounded HTTP request passed to the injected transport.
struct HttpRequest {
    std::string method = "POST"; ///< HTTP method.
    std::string url; ///< Authenticated Bot API URL; do not log it.
    std::string body; ///< Form-encoded body bounded by `max_request_bytes`.
    std::vector<std::pair<std::string, std::string>> headers; ///< Request headers.
    std::chrono::milliseconds timeout{10000}; ///< Per-request timeout.
    std::size_t max_response_bytes{}; ///< Hard response-body limit for the adapter.
};

/// \brief HTTP response returned by the injected transport.
struct HttpResponse {
    int status_code{}; ///< HTTP status code.
    std::vector<std::pair<std::string, std::string>> headers; ///< Header names and values.
    std::string body; ///< Bounded response body.
};

/// \brief Result of one HTTP exchange, including whether a request may have been sent.
struct HttpExchange {
    std::optional<HttpResponse> response; ///< Present when an HTTP response was received.
    std::optional<HttpFailureKind> failure; ///< Present when the adapter failed.
    bool request_may_have_been_sent = false; ///< True after an ambiguous transport failure.
};

/// \brief Injectable HTTP boundary used by `TelegramBotApi`.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    /// \brief Executes one bounded request without owning Telegram credentials.
    /// \param request Request with an authenticated URL and strict response limit.
    /// \param cancellation Cooperative cancellation token.
    /// \return Response or typed transport failure; implementations must not throw.
    [[nodiscard]] virtual HttpExchange execute(const HttpRequest& request,
                                               const CancellationToken& cancellation) noexcept = 0;
};

/// \brief High-level classification of one Bot API call.
enum class TelegramOutcome {
    Succeeded, ///< Telegram returned a successful HTTP status.
    InvalidRequest, ///< Local bounds or required argument failed.
    ClientError, ///< Telegram returned a 4xx status other than 429.
    ServerError, ///< Telegram returned a 5xx status.
    RateLimited, ///< Telegram returned 429; inspect `retry_after_seconds`.
    TransportFailure, ///< No response and no evidence of request acceptance.
    Timeout, ///< Timeout before an ambiguous send was possible.
    Cancelled, ///< Cancellation before an ambiguous send was possible.
    UnknownOutcome, ///< Request may have been accepted but result is unknown.
};

/// \brief Typed result that never exposes response bodies or credentials.
struct TelegramResult {
    TelegramOutcome outcome = TelegramOutcome::TransportFailure; ///< Typed call outcome.
    int http_status = 0; ///< Zero when no HTTP response was received.
    std::optional<std::uint32_t> retry_after_seconds; ///< Parsed bounded Retry-After value.
    std::optional<HttpFailureKind> transport_failure; ///< Underlying adapter failure, if any.

    /// \brief Reports successful Telegram acknowledgement.
    /// \return True only for `TelegramOutcome::Succeeded`.
    [[nodiscard]] bool succeeded() const noexcept
    {
        return outcome == TelegramOutcome::Succeeded;
    }
};

/// \brief Limits and timeout policy for one Bot API client.
struct TelegramBotApiConfig {
    std::size_t max_request_bytes = 16 * 1024; ///< Maximum encoded request body.
    std::size_t max_response_bytes = 64 * 1024; ///< Maximum response body accepted.
    std::size_t max_text_bytes = 4096; ///< Maximum message text before encoding.
    std::size_t max_callback_id_bytes = 256; ///< Maximum callback-query identifier.
    std::chrono::milliseconds timeout{10000}; ///< Per-request transport deadline.
};

/// \brief Bounded Bot API client for the three M3b-a methods.
class TelegramBotApi {
public:
    /// \brief Creates a client borrowing an application-owned HTTP transport.
    /// \param bot_token Telegram bot token; retained only by this client.
    /// \param transport Injectable HTTP boundary.
    /// \param config Request, response, and timeout limits.
    TelegramBotApi(std::string bot_token,
                   IHttpTransport& transport,
                   TelegramBotApiConfig config = {});
    TelegramBotApi(const TelegramBotApi&) = delete;
    TelegramBotApi& operator=(const TelegramBotApi&) = delete;

    /// \brief Sends a new text message.
    /// \param chat_id Destination chat identity.
    /// \param text UTF-8 text to send.
    /// \param cancellation Cooperative cancellation token.
    /// \return Typed acknowledgement or conservative failure outcome.
    [[nodiscard]] TelegramResult send_message(ChatId chat_id,
                                              std::string_view text,
                                              const CancellationToken& cancellation = {}) const noexcept;

    /// \brief Edits a known text message.
    /// \param chat_id Chat containing the known message.
    /// \param message_id Telegram message number.
    /// \param text Replacement UTF-8 text.
    /// \param cancellation Cooperative cancellation token.
    /// \return Typed acknowledgement or conservative failure outcome.
    [[nodiscard]] TelegramResult edit_message_text(ChatId chat_id,
                                                    std::int64_t message_id,
                                                    std::string_view text,
                                                    const CancellationToken& cancellation = {}) const noexcept;

    /// \brief Answers a callback query without replaying application handlers.
    /// \param callback_query_id Telegram callback-query identity.
    /// \param cancellation Cooperative cancellation token.
    /// \return Typed acknowledgement or conservative failure outcome.
    [[nodiscard]] TelegramResult answer_callback_query(
        std::string_view callback_query_id,
        const CancellationToken& cancellation = {}) const noexcept;

private:
    [[nodiscard]] TelegramResult call(std::string_view method,
                                      std::string body,
                                      const CancellationToken& cancellation) const noexcept;

    std::string m_bot_token;
    IHttpTransport& m_transport;
    TelegramBotApiConfig m_config;
};

} // namespace imtg::telegram_transport

#endif // IMTG_TELEGRAM_TRANSPORT_TELEGRAM_BOT_API_HPP
