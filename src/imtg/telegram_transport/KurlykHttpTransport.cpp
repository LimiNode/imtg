#include <imtg/telegram_transport/KurlykHttpTransport.hpp>

#include <kurlyk.hpp>

#include <future>
#include <limits>
#include <stdexcept>

namespace imtg::telegram_transport {
namespace {

HttpExchange failure(HttpFailureKind kind, bool may_have_been_sent) noexcept
{
    return {std::nullopt, kind, may_have_been_sent};
}

bool has_control_bytes(const std::string& value) noexcept
{
    return value.find_first_of(std::string_view{"\r\n\0", 3}) != std::string::npos;
}

HttpExchange convert_response(kurlyk::HttpResponsePtr response)
{
    if (!response) {
        return failure(HttpFailureKind::Network, true);
    }
    const auto error = response->error_code;
    if (error == kurlyk::utils::make_error_code(kurlyk::utils::ClientError::ResponseTooLarge)) {
        return failure(HttpFailureKind::ResponseTooLarge, true);
    }
    if (error == kurlyk::utils::make_error_code(kurlyk::utils::ClientError::CancelledByUser)
        || error == kurlyk::utils::make_error_code(kurlyk::utils::ClientError::AbortedDuringDestruction)) {
        return failure(HttpFailureKind::Cancelled, true);
    }
    if (error == kurlyk::utils::make_error_code(CURLE_OPERATION_TIMEDOUT)) {
        return failure(HttpFailureKind::Timeout, true);
    }
    // HTTP error statuses are complete responses, unlike synthetic curl statuses.
    if (error && error.category() != kurlyk::utils::make_http_error(400).category()) {
        return failure(HttpFailureKind::Network, true);
    }
    if (response->status_code < 100 || response->status_code > 599) {
        return failure(HttpFailureKind::MalformedResponse, true);
    }
    HttpResponse result;
    result.status_code = static_cast<int>(response->status_code);
    result.body = std::move(response->content);
    result.headers.reserve(response->headers.size());
    for (const auto& header : response->headers) {
        result.headers.emplace_back(header.first, header.second);
    }
    return {std::move(result), std::nullopt, true};
}

} // namespace

KurlykHttpTransport::KurlykHttpTransport(std::size_t max_request_bytes)
    : m_max_request_bytes(max_request_bytes)
{
    if (max_request_bytes == 0) {
        throw std::invalid_argument("KurlykHttpTransport requires a finite request-body bound");
    }
}

HttpExchange KurlykHttpTransport::execute(const HttpRequest& request,
                                         const CancellationToken& cancellation) noexcept
{
    bool submission_started = false;
    try {
        if (cancellation.is_cancelled()) {
            return failure(HttpFailureKind::Cancelled, false);
        }
        if (request.body.size() > m_max_request_bytes) {
            return failure(HttpFailureKind::RequestTooLarge, false);
        }
        if (request.timeout.count() <= 0) {
            return failure(HttpFailureKind::Timeout, false);
        }
        const auto started = std::chrono::steady_clock::now();
        const auto remaining_range = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::time_point::max() - started);
        const auto timeout_seconds = request.timeout.count() / 1000
            + (request.timeout.count() % 1000 != 0 ? 1 : 0);
        if (request.max_response_bytes == 0 || request.timeout > remaining_range
            || timeout_seconds > (std::numeric_limits<long>::max)()
            || request.method.empty() || request.url.empty()
            || has_control_bytes(request.method) || has_control_bytes(request.url)) {
            return failure(HttpFailureKind::Network, false);
        }
        const auto deadline = started + request.timeout;
        auto outgoing = std::make_unique<kurlyk::HttpRequest>();
        auto& manager = kurlyk::HttpRequestManager::get_instance();
        auto& worker = kurlyk::core::NetworkWorker::get_instance();
        const auto group = manager.generate_group_id();
        outgoing->group_id = group;
        outgoing->request_id = manager.generate_request_id();
        outgoing->url = request.url;
        outgoing->method = request.method;
        outgoing->content = request.body;
        outgoing->timeout = static_cast<long>(timeout_seconds);
        outgoing->connect_timeout = outgoing->timeout;
        outgoing->max_response_body_bytes = request.max_response_bytes;
        outgoing->retry_attempts = 0;
        outgoing->follow_location = false;
        outgoing->verbose = false;
        outgoing->debug_header = false;
        for (const auto& header : request.headers) {
            if (header.first.empty() || has_control_bytes(header.first)
                || has_control_bytes(header.second)) {
                return failure(HttpFailureKind::Network, false);
            }
            outgoing->headers.emplace(header.first, header.second);
        }
        auto completion = std::make_shared<std::promise<kurlyk::HttpResponsePtr>>();
        auto future = completion->get_future();
        if (cancellation.is_cancelled()) {
            return failure(HttpFailureKind::Cancelled, false);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return failure(HttpFailureKind::Timeout, false);
        }
        submission_started = true;
        const auto submitted = manager.submit_request(std::move(outgoing),
            [completion](kurlyk::HttpResponsePtr response) noexcept {
                if (response && response->ready) {
                    try {
                        completion->set_value(std::move(response));
                    } catch (...) {
                        // Isolate a broken backend callback contract from its worker.
                    }
                }
            });
        if (!submitted.accepted) {
            return failure(HttpFailureKind::Network, false);
        }
        worker.notify();
        for (;;) {
            if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                return convert_response(future.get());
            }
            const bool cancelled = cancellation.is_cancelled();
            if (cancelled || std::chrono::steady_clock::now() >= deadline) {
                manager.cancel_requests_by_group_id(group, nullptr);
                worker.notify();
                return failure(cancelled ? HttpFailureKind::Cancelled : HttpFailureKind::Timeout, true);
            }
            future.wait_until((std::min)(deadline,
                std::chrono::steady_clock::now() + std::chrono::milliseconds(5)));
        }
    } catch (...) {
        return failure(HttpFailureKind::Network, submission_started);
    }
}

} // namespace imtg::telegram_transport
