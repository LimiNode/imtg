#include <imtg/logging.hpp>

#include <logit.hpp>

namespace imtg::logging {

namespace {

struct SafeIdentity {
    std::uint64_t bot_id{};
    std::int64_t chat_id{};
    std::uint64_t update_id{};
};

const char* error_code_name(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::InvalidArgument: return "invalid_argument";
    case ErrorCode::InvalidEncoding: return "invalid_encoding";
    case ErrorCode::TruncatedEncoding: return "truncated_encoding";
    case ErrorCode::UnsupportedVersion: return "unsupported_version";
    case ErrorCode::DuplicateId: return "duplicate_id";
    case ErrorCode::InvalidState: return "invalid_state";
    case ErrorCode::ScopeMismatch: return "scope_mismatch";
    case ErrorCode::UnknownAction: return "unknown_action";
    case ErrorCode::MalformedAction: return "malformed_action";
    case ErrorCode::PayloadVersionMismatch: return "payload_version_mismatch";
    case ErrorCode::ExpiredAction: return "expired_action";
    case ErrorCode::HandlerUnavailable: return "handler_unavailable";
    case ErrorCode::DispatchRejected: return "dispatch_rejected";
    case ErrorCode::QueueFull: return "queue_full";
    case ErrorCode::RuntimeFailure: return "runtime_failure";
    case ErrorCode::StorageNotFound: return "storage_not_found";
    case ErrorCode::StorageConflict: return "storage_conflict";
    case ErrorCode::StorageReadOnly: return "storage_read_only";
    case ErrorCode::StorageStaleClaim: return "storage_stale_claim";
    case ErrorCode::StorageUnavailable: return "storage_unavailable";
    case ErrorCode::StorageSchemaFailure: return "storage_schema_failure";
    case ErrorCode::StorageSerializationFailure: return "storage_serialization_failure";
    case ErrorCode::StorageRecordTooLarge: return "storage_record_too_large";
    case ErrorCode::StoragePermissionDenied: return "storage_permission_denied";
    case ErrorCode::StorageUnknownCommit: return "storage_unknown_commit";
    case ErrorCode::StorageRevisionExhausted: return "storage_revision_exhausted";
    }
    return "unknown_error";
}

SafeIdentity update_identity(const telegram::Update& update) noexcept
{
    SafeIdentity identity{update.bot_id.value, 0, update.update_id};
    try {
        std::visit([&identity](const auto& payload) {
            identity.chat_id = payload.chat_id.value;
        }, update.payload);
    } catch (...) {
        // A malformed variant must not cross the observer boundary.
    }
    return identity;
}

SafeIdentity conversation_identity(const ConversationKey& key) noexcept
{
    return SafeIdentity{key.bot_id.value, key.chat_id.value, 0};
}

LogEvent admission_event(telegram::UpdateOutcome outcome) noexcept
{
    switch (outcome) {
    case telegram::UpdateOutcome::Accepted: return LogEvent::UpdateAccepted;
    case telegram::UpdateOutcome::Duplicate: return LogEvent::UpdateDuplicate;
    case telegram::UpdateOutcome::Invalid: return LogEvent::UpdateInvalid;
    case telegram::UpdateOutcome::Rejected: return LogEvent::UpdateRejected;
    case telegram::UpdateOutcome::Unauthorized: return LogEvent::UpdateUnauthorized;
    case telegram::UpdateOutcome::Stale: return LogEvent::UpdateStale;
    case telegram::UpdateOutcome::QueueFull: return LogEvent::UpdateQueueFull;
    case telegram::UpdateOutcome::Closed: return LogEvent::UpdateClosed;
    case telegram::UpdateOutcome::HandlerFailed: return LogEvent::UpdateHandlerFailed;
    }
    return LogEvent::UpdateRejected;
}

bool is_error(LogEvent event) noexcept
{
    return event == LogEvent::UpdateHandlerFailed
        || event == LogEvent::RuntimeTaskFailed
        || event == LogEvent::RuntimeFailStop;
}

bool is_warning(LogEvent event) noexcept
{
    return event == LogEvent::UpdateDuplicate
        || event == LogEvent::UpdateInvalid
        || event == LogEvent::UpdateRejected
        || event == LogEvent::UpdateUnauthorized
        || event == LogEvent::UpdateStale
        || event == LogEvent::UpdateQueueFull
        || event == LogEvent::UpdateClosed;
}

void emit(LogEvent event,
          const LogitOptions& options,
          const SafeIdentity* identity,
          const char* error_code = nullptr) noexcept
{
    try {
        const auto code = log_event_code(event);
        if (options.include_safe_identifiers && identity != nullptr) {
            if (error_code != nullptr) {
                if (is_error(event)) {
                    LOGIT_ERROR_TAG("imtg.event", LOGIT_TAG("code", code),
                                    LOGIT_TAG("error", error_code),
                                    LOGIT_TAG("bot_id", identity->bot_id),
                                    LOGIT_TAG("chat_id", identity->chat_id),
                                    LOGIT_TAG("update_id", identity->update_id));
                } else {
                    LOGIT_WARN_TAG("imtg.event", LOGIT_TAG("code", code),
                                   LOGIT_TAG("error", error_code),
                                   LOGIT_TAG("bot_id", identity->bot_id),
                                   LOGIT_TAG("chat_id", identity->chat_id),
                                   LOGIT_TAG("update_id", identity->update_id));
                }
            } else if (is_error(event)) {
                LOGIT_ERROR_TAG("imtg.event", LOGIT_TAG("code", code),
                                LOGIT_TAG("bot_id", identity->bot_id),
                                LOGIT_TAG("chat_id", identity->chat_id),
                                LOGIT_TAG("update_id", identity->update_id));
            } else if (is_warning(event)) {
                LOGIT_WARN_TAG("imtg.event", LOGIT_TAG("code", code),
                               LOGIT_TAG("bot_id", identity->bot_id),
                               LOGIT_TAG("chat_id", identity->chat_id),
                               LOGIT_TAG("update_id", identity->update_id));
            } else {
                LOGIT_INFO_TAG("imtg.event", LOGIT_TAG("code", code),
                               LOGIT_TAG("bot_id", identity->bot_id),
                               LOGIT_TAG("chat_id", identity->chat_id),
                               LOGIT_TAG("update_id", identity->update_id));
            }
            return;
        }

        if (error_code != nullptr) {
            if (is_error(event)) {
                LOGIT_ERROR_TAG("imtg.event", LOGIT_TAG("code", code),
                                LOGIT_TAG("error", error_code));
            } else {
                LOGIT_WARN_TAG("imtg.event", LOGIT_TAG("code", code),
                               LOGIT_TAG("error", error_code));
            }
        } else if (is_error(event)) {
            LOGIT_ERROR_TAG("imtg.event", LOGIT_TAG("code", code));
        } else if (is_warning(event)) {
            LOGIT_WARN_TAG("imtg.event", LOGIT_TAG("code", code));
        } else {
            LOGIT_INFO_TAG("imtg.event", LOGIT_TAG("code", code));
        }
    } catch (...) {
        // Diagnostics are strictly best-effort and cannot affect imtg work.
    }
}

} // namespace

const char* log_event_code(LogEvent event) noexcept
{
    switch (event) {
    case LogEvent::UpdateAccepted: return "telegram.update.accepted";
    case LogEvent::UpdateDuplicate: return "telegram.update.duplicate";
    case LogEvent::UpdateInvalid: return "telegram.update.invalid";
    case LogEvent::UpdateRejected: return "telegram.update.rejected";
    case LogEvent::UpdateUnauthorized: return "telegram.update.unauthorized";
    case LogEvent::UpdateStale: return "telegram.update.stale";
    case LogEvent::UpdateQueueFull: return "telegram.update.queue_full";
    case LogEvent::UpdateClosed: return "telegram.update.closed";
    case LogEvent::UpdateHandlerFailed: return "telegram.update.handler_failed";
    case LogEvent::RuntimeTaskFailed: return "runtime.task_failed";
    case LogEvent::RuntimeShutdown: return "runtime.shutdown";
    case LogEvent::RuntimeFailStop: return "runtime.fail_stop";
    }
    return "imtg.unknown";
}

LogitAdapter::LogitAdapter(LogitOptions options) noexcept
    : m_options(options)
{
}

ReferenceRuntime::ErrorFunction LogitAdapter::runtime_error_sink() const
{
    const auto options = m_options;
    return [options](const ConversationKey& key, const Error& error) noexcept {
        const auto identity = conversation_identity(key);
        emit(LogEvent::RuntimeTaskFailed, options, &identity, error_code_name(error.code));
    };
}

telegram::UpdateRouter::OutcomeSink LogitAdapter::outcome_sink() const
{
    const auto options = m_options;
    return [options](telegram::UpdateOutcome outcome,
                     const telegram::UpdateContext& context,
                     const Error& error) noexcept {
        const auto identity = update_identity(context.update);
        emit(admission_event(outcome), options, &identity, error_code_name(error.code));
    };
}

void LogitAdapter::record_update_admission(telegram::UpdateOutcome outcome,
                                           const telegram::Update& update) const noexcept
{
    const auto identity = update_identity(update);
    emit(admission_event(outcome), m_options, &identity);
}

void LogitAdapter::record_runtime_shutdown(const Result<void>& result) const noexcept
{
    if (result) {
        emit(LogEvent::RuntimeShutdown, m_options, nullptr);
    } else {
        emit(LogEvent::RuntimeFailStop, m_options, nullptr, error_code_name(result.error().code));
    }
}

} // namespace imtg::logging
