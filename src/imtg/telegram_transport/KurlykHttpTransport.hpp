#ifndef IMTG_TELEGRAM_TRANSPORT_KURLYK_HTTP_TRANSPORT_HPP
#define IMTG_TELEGRAM_TRANSPORT_KURLYK_HTTP_TRANSPORT_HPP

/// \file imtg/telegram_transport/KurlykHttpTransport.hpp
/// \brief Optional receive-bounded kurlyk implementation of the HTTP boundary.

#include "TelegramBotApi.hpp"

namespace imtg::telegram_transport {

/// \brief Executes isolated HTTP exchanges through the shared kurlyk worker.
///
/// Include this standalone header and link `imtg::telegram_kurlyk`. No vendor
/// types enter the public API. The application owns kurlyk initialization,
/// asynchronous worker progress, global admission limits and final shutdown;
/// it must stop callers before stopping that worker. Calls may overlap, but
/// must not run on the kurlyk worker or hold a storage transaction open.
///
/// Each call owns a separate cancellation group, never retries, disables
/// redirects and retains TLS verification. Cancellation/deadline queues group
/// cancellation without waiting for worker quiescence: a late callback owns
/// only its completion state, never the request, token or transport object.
/// The application must keep the shared worker progressing to reclaim pending
/// operations. No URL, token, request/response body or backend error text is
/// logged or returned as diagnostics. The receive-time limit bounds body
/// bytes, not backend header buffers or the application's total concurrency.
class KurlykHttpTransport final : public IHttpTransport {
public:
    /// \brief Sets the finite request-body bound applied before submission.
    /// \param max_request_bytes Maximum body size; must be nonzero.
    /// \throws std::invalid_argument If the bound is zero.
    explicit KurlykHttpTransport(std::size_t max_request_bytes = 16 * 1024);

    /// \brief Performs one request with a mandatory receive-time body limit.
    /// \param request Borrowed input, copied before asynchronous submission.
    ///        Response limit and timeout must be positive and representable.
    /// \param cancellation Borrowed token observed until this call returns.
    /// \return Exactly one response or typed failure. After submission begins,
    ///         failures conservatively allow transmission unless admission was
    ///         explicitly rejected. Cancellation never proves remote rollback.
    /// \note The deadline includes local waiting; actual cancellation cleanup
    ///       depends on shared worker progress. No callbacks access borrowed input.
    [[nodiscard]] HttpExchange execute(const HttpRequest& request,
                                       const CancellationToken& cancellation) noexcept override;

private:
    const std::size_t m_max_request_bytes;
};

} // namespace imtg::telegram_transport

#endif
