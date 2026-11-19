#ifndef IMTG_LOGGING_LOGIT_ADAPTER_HPP
#define IMTG_LOGGING_LOGIT_ADAPTER_HPP

/// \file imtg/logging/LogitAdapter.hpp
/// \brief Optional LogIt++ observers for runtime and Telegram diagnostics.

#include <imtg/core.hpp>
#include <imtg/runtime.hpp>
#include <imtg/telegram.hpp>

#include <cstdint>
#include <functional>

namespace imtg::logging {

/// \brief Stable, sanitized event names emitted by the optional adapter.
enum class LogEvent {
    UpdateAccepted,
    UpdateDuplicate,
    UpdateInvalid,
    UpdateRejected,
    UpdateUnauthorized,
    UpdateStale,
    UpdateQueueFull,
    UpdateClosed,
    UpdateHandlerFailed,
    RuntimeTaskFailed,
    RuntimeShutdown,
    RuntimeFailStop,
};

/// \brief Selects the amount of identity data emitted by the adapter.
struct LogitOptions {
    /// Emit bot, chat and update identifiers in addition to the event code.
    /// Text, callback data, credentials and error messages are never emitted.
    bool include_safe_identifiers = false;
};

/// \brief Binds existing imtg observers to LogIt++ without changing core APIs.
///
/// The adapter only emits fixed event codes and, when explicitly enabled,
/// numeric bot/chat/update identifiers. It never emits message text, callback
/// payloads, credentials, HTTP headers, or `Error::message`. LogIt++ owns its
/// global logger lifecycle; destroying or shutting down an imtg runtime does
/// not shut down LogIt++.
/// Use `<imtg/logging.hpp>` or this standalone header and link `imtg::logit`.
/// Installed consumers request the `logit` CMake component. Callbacks capture
/// options by value and may outlive the adapter. Applications must select bounded,
/// non-waiting drop policies and avoid blocking backends on runtime workers.
class LogitAdapter {
public:
    /// \brief Creates a value-owned observer adapter.
    /// \param options Sanitization and correlation options.
    explicit LogitAdapter(LogitOptions options = {}) noexcept;

    /// \brief Creates a runtime failure observer.
    /// \return Callback suitable for `ReferenceRuntime::ErrorFunction`.
    /// \note The callback catches all logger failures and never throws.
    [[nodiscard]] ReferenceRuntime::ErrorFunction runtime_error_sink() const;

    /// \brief Creates a serialized Telegram outcome observer.
    /// \return Callback suitable for `telegram::UpdateRouter::OutcomeSink`.
    /// \note The callback catches all logger failures and never throws.
    [[nodiscard]] telegram::UpdateRouter::OutcomeSink outcome_sink() const;

    /// \brief Records the synchronous result of `UpdateRouter::receive`.
    /// \param outcome Admission result returned by the router.
    /// \param update Owned update supplied to the router.
    /// \note Only fixed event codes and optional numeric identifiers are logged.
    void record_update_admission(telegram::UpdateOutcome outcome,
                                 const telegram::Update& update) const noexcept;

    /// \brief Records runtime shutdown or fail-stop completion.
    /// \param result Shutdown result returned by `ReferenceRuntime::shutdown`.
    /// \note This method never calls LogIt++ shutdown.
    void record_runtime_shutdown(const Result<void>& result) const noexcept;

private:
    LogitOptions m_options;
};

/// \brief Returns the stable wire name for one adapter event.
/// \param event Event to name.
/// \return Null-terminated stable event code.
[[nodiscard]] const char* log_event_code(LogEvent event) noexcept;

} // namespace imtg::logging

#endif // IMTG_LOGGING_LOGIT_ADAPTER_HPP
