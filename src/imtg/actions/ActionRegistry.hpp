#ifndef IMTG_ACTIONS_ACTION_REGISTRY_HPP
#define IMTG_ACTIONS_ACTION_REGISTRY_HPP

/// \file imtg/actions/ActionRegistry.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "ActionContext.hpp"
#include "ActionHandle.hpp"
#include "PayloadCodec.hpp"
#include "token.hpp"
#include <imtg/core.hpp>

#include <charconv>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace imtg {

/// \brief Application-level registry for restart-safe, typed action routes.
class ActionRegistry {
public:
    // The registry must outlive concurrent handle validation and UI binding.
    // Destruction is an external-owner operation; callers provide quiescence.
    /// \brief Creates a registry borrowing its token codec and clock.
    /// \param token_codec Codec that must outlive the registry and concurrent calls.
    /// \param clock Clock that must outlive the registry and concurrent calls.
    /// \thread_safety Registration, validation, token creation and invocation are synchronized.
    ActionRegistry(const ITokenCodec& token_codec, const IClock& clock)
        : m_token_codec(token_codec), m_clock(clock), m_identity(std::make_shared<int>(0))
    {
    }

    template <typename Payload>
    /// \brief Typed synchronous action handler.
    using Handler = std::function<Result<void>(ActionContext&, const Payload&)>;

    template <typename Payload>
    /// \brief Registers one canonical action route and returns its generation-bound handle.
    /// \param action_id Canonical `name.vN` route, unique in this registry.
    /// \param codec Complete typed payload codec.
    /// \param handler Synchronous handler; its context is invalid after return.
    /// \return Handle or a validation/duplicate error.
    [[nodiscard]] Result<ActionHandle<Payload>> register_action(
        ActionId action_id,
        PayloadCodec<Payload> codec,
        Handler<Payload> handler)
    {
        if (!is_canonical_route(action_id.value)) {
            return Error{ErrorCode::InvalidArgument,
                         "action id must use canonical name.vN syntax"};
        }
        if (!codec.encode || !codec.decode || !handler) {
            return Error{ErrorCode::InvalidArgument, "action registration is incomplete"};
        }

        const std::uint32_t payload_version = codec.version;
        auto encode = codec.encode;
        auto entry = std::make_shared<Entry>();
        entry->payload_version = payload_version;
        entry->invoke = [codec = std::move(codec), handler = std::move(handler)](
                            ActionContext& context, std::string_view payload) -> Result<void> {
            const auto decoded = codec.decode(payload);
            if (!decoded) {
                return decoded.error();
            }
            return handler(context, decoded.value());
        };

        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (m_entries.find(action_id.value) != m_entries.end()) {
            return Error{ErrorCode::InvalidArgument, "action id is already registered"};
        }
        const auto generation = m_next_generation++;
        entry->generation = generation;

        ActionHandle<Payload> handle;
        handle.m_action_id = action_id;
        handle.m_payload_version = payload_version;
        handle.m_generation = generation;
        handle.m_owner = this;
        handle.m_registry_identity = m_identity;
        handle.m_encode = std::move(encode);
        handle.m_validate = &detail::validate_action_handle;

        // Publish only after all handle state is prepared. The remaining
        // operation is the map insertion itself; failures leave no route.
        m_entries.emplace(action_id.value, std::move(entry));
        return handle;
    }

    template <typename Payload>
    /// \brief Encodes a registered typed action into an external token.
    /// \param handle Current handle owned by this registry.
    /// \param payload Typed application payload.
    /// \param expires_at Optional system-clock expiry checked during invocation.
    /// \return External token or a stale-handle/codec error.
    [[nodiscard]] Result<std::string> make_token(
        const ActionHandle<Payload>& handle,
        const Payload& payload,
        std::optional<ClockTime> expires_at = std::nullopt) const
    {
        if (!handle.is_valid() || handle.m_owner != this
            || handle.m_registry_identity.lock() != m_identity) {
            return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
        }
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            const auto found = m_entries.find(handle.action_id().value);
            if (found == m_entries.end()
                || found->second->payload_version != handle.payload_version()
                || found->second->generation != handle.m_generation) {
                return Error{ErrorCode::UnknownAction, "action route is not registered"};
            }
        }
        const auto encoded = handle.m_encode(payload);
        if (!encoded) {
            return encoded.error();
        }
        return m_token_codec.encode(
            DecodedAction{handle.action_id(), handle.payload_version(), encoded.value(), expires_at});
    }

    /// \brief Decodes, validates and synchronously invokes one action token.
    /// \param token Untrusted external token.
    /// \param sink Command sink valid only for the duration of this call.
    /// \return Handler/sink result or a token, expiry or registration error.
    [[nodiscard]] Result<void> invoke(std::string_view token,
                                      ActionContext::DispatchSink sink) const;

    /// \brief Removes one route and permanently stales handles to its generation.
    /// \param action_id Route to unregister.
    /// \return Success or `UnknownAction` when absent.
    [[nodiscard]] Result<void> unregister_action(const ActionId& action_id);

    template <typename Payload>
    /// \brief Checks that a typed handle still names the current registration generation.
    /// \param handle Handle to validate.
    /// \return Success or an ownership, stale-generation or version error.
    [[nodiscard]] Result<void> validate_handle(const ActionHandle<Payload>& handle) const
    {
        if (!handle.is_valid() || handle.m_owner != this) {
            return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
        }
        return validate_handle_state(handle.action_id(), handle.payload_version(),
                                     handle.m_generation, handle.m_registry_identity);
    }

private:
    friend Result<void> detail::validate_action_handle(
        const ActionRegistry*,
        const ActionId&,
        std::uint32_t,
        std::uint64_t,
        const std::weak_ptr<const void>&);

    static bool is_canonical_route(std::string_view route) noexcept
    {
        const auto marker = route.rfind(".v");
        if (marker == std::string_view::npos || marker == 0 || marker + 2 >= route.size()) {
            return false;
        }
        const auto digits = route.substr(marker + 2);
        if (digits.size() > 1 && digits.front() == '0') {
            return false;
        }
        std::uint32_t version = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), version);
        return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size()
            && version != 0;
    }

    struct Entry {
        std::uint32_t payload_version{};
        std::uint64_t generation{};
        std::function<Result<void>(ActionContext&, std::string_view)> invoke;
    };

    [[nodiscard]] Result<void> validate_handle_state(
        const ActionId& action_id,
        std::uint32_t payload_version,
        std::uint64_t generation,
        const std::weak_ptr<const void>& registry_identity) const;

    const ITokenCodec& m_token_codec;
    const IClock& m_clock;
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Entry>> m_entries;
    std::uint64_t m_next_generation = 1;
    std::shared_ptr<const void> m_identity;
};

} // namespace imtg

#endif // IMTG_ACTIONS_ACTION_REGISTRY_HPP
