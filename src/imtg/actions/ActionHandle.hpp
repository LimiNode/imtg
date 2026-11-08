#ifndef IMTG_ACTIONS_ACTION_HANDLE_HPP
#define IMTG_ACTIONS_ACTION_HANDLE_HPP

#include <imtg/actions/token.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace imtg {

class ActionRegistry;

template <typename Payload>
/// \brief Registry-owned typed capability for binding and encoding an action.
/// \details A handle becomes stale after unregistration/re-registration and must not
/// outlive concurrent access to its owning registry without external quiescence.
class ActionHandle {
public:
    ActionHandle() = default;

    /// \brief Reports whether the handle has basic identity and encoder state.
    /// \return `true` when identity and encoder state are present.
    /// \note Use `validate()` to detect stale or unregistered handles.
    [[nodiscard]] bool is_valid() const noexcept
    {
        return !m_action_id.value.empty() && static_cast<bool>(m_encode);
    }

    /// \brief Returns the stable action route identity.
    /// \return Route captured at registration.
    [[nodiscard]] const ActionId& action_id() const noexcept
    {
        return m_action_id;
    }

    /// \brief Returns the payload schema version captured at registration.
    /// \return Registered payload schema version.
    [[nodiscard]] std::uint32_t payload_version() const noexcept
    {
        return m_payload_version;
    }

    /// \brief Encodes a typed payload using the captured codec.
    /// \param payload Typed application payload.
    /// \return Encoded bytes or a codec/invalid-handle error.
    [[nodiscard]] Result<std::string> encode_payload(const Payload& payload) const
    {
        if (!m_encode) {
            return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
        }
        return m_encode(payload);
    }

    /// \brief Validates ownership, generation and payload version against the registry.
    /// \return Success only while the original registration remains current.
    [[nodiscard]] Result<void> validate() const;

private:
    friend class ActionRegistry;

    ActionId m_action_id;
    std::uint32_t m_payload_version{};
    std::uint64_t m_generation{};
    const ActionRegistry* m_owner = nullptr;
    std::weak_ptr<const void> m_registry_identity;
    std::function<Result<std::string>(const Payload&)> m_encode;
};

} // namespace imtg

#endif // IMTG_ACTIONS_ACTION_HANDLE_HPP
