#include <imtg/actions.hpp>

namespace imtg {

namespace detail {

Result<void> validate_action_handle(
    const ActionRegistry* owner,
    const ActionId& action_id,
    std::uint32_t payload_version,
    std::uint64_t generation,
    const std::weak_ptr<const void>& registry_identity)
{
    if (owner == nullptr) {
        return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
    }
    return owner->validate_handle_state(action_id, payload_version, generation,
                                        registry_identity);
}

} // namespace detail

Result<void> ActionRegistry::validate_handle_state(
    const ActionId& action_id,
    std::uint32_t payload_version,
    std::uint64_t generation,
    const std::weak_ptr<const void>& registry_identity) const
{
    if (registry_identity.expired() || registry_identity.lock() != m_identity) {
        return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
    }
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    const auto found = m_entries.find(action_id.value);
    if (found == m_entries.end()) {
        return Error{ErrorCode::UnknownAction, "action route is not registered"};
    }
    if (found->second->generation != generation) {
        return Error{ErrorCode::InvalidArgument, "action handle is stale"};
    }
    if (found->second->payload_version != payload_version) {
        return Error{ErrorCode::PayloadVersionMismatch,
                     "action payload version is incompatible"};
    }
    return Result<void>();
}

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
