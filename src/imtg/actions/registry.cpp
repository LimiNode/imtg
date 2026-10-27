#include "imtg/actions/registry.hpp"

namespace imtg {

Result<void> ActionRegistry::invoke(std::string_view token,
                                    ActionContext::DispatchSink sink) const
{
    const auto decoded = m_token_codec.decode(token);
    if (!decoded) {
        return decoded.error();
    }
    if (decoded.value().action_id.value.empty()) {
        return Error{ErrorCode::UnknownAction, "action id is empty"};
    }
    if (decoded.value().expires_at.has_value()
        && m_clock.now() >= decoded.value().expires_at.value()) {
        return Error{ErrorCode::ExpiredAction, "action token has expired"};
    }

    std::shared_ptr<Entry> entry;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto found = m_entries.find(decoded.value().action_id.value);
        if (found == m_entries.end()) {
            return Error{ErrorCode::UnknownAction, "action route is not registered"};
        }
        entry = found->second;
    }
    if (entry->payload_version != decoded.value().payload_version) {
        return Error{ErrorCode::PayloadVersionMismatch, "action payload version is incompatible"};
    }

    ActionContext context(std::move(sink));
    const auto result = entry->invoke(context, decoded.value().payload);
    context.deactivate();
    return result;
}

Result<void> ActionRegistry::unregister_action(const ActionId& action_id)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_entries.erase(action_id.value) == 0) {
        return Error{ErrorCode::UnknownAction, "action route is not registered"};
    }
    return Result<void>();
}

} // namespace imtg
