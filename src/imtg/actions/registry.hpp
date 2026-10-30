#ifndef IMTG_ACTIONS_REGISTRY_HPP
#define IMTG_ACTIONS_REGISTRY_HPP

#include "imtg/actions/token.hpp"
#include "imtg/core/clock.hpp"

#include <functional>
#include <charconv>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace imtg {

/// \brief Backend-neutral command emitted by an action handler.
struct ActionCommand {
    std::string type; ///< Application-defined command route.
    std::string payload; ///< Application-defined command bytes.
};

class ActionRegistry;

/// \brief Invocation-scoped command capability; it cannot be used after invoke returns.
class ActionContext {
public:
    /// \brief Synchronous sink that accepts commands during one invocation.
    using DispatchSink = std::function<Result<void>(const ActionCommand&)>;

    /// \brief Creates an invocation-scoped context around a command sink.
    /// \param sink Sink called synchronously by `dispatch()`.
    explicit ActionContext(DispatchSink sink) : m_sink(std::move(sink)) {}
    ActionContext(const ActionContext&) = delete;
    ActionContext& operator=(const ActionContext&) = delete;

    /// \brief Dispatches one command while the invocation remains active.
    /// \param command Command transferred to the configured sink.
    /// \return Sink result or `DispatchRejected` after the invocation ends.
    [[nodiscard]] Result<void> dispatch(ActionCommand command)
    {
        if (!m_active) {
            return Error{ErrorCode::DispatchRejected, "action context is no longer active"};
        }
        if (!m_sink) {
            return Error{ErrorCode::DispatchRejected, "action context has no dispatch sink"};
        }
        return m_sink(command);
    }

private:
    friend class ActionRegistry;

    void deactivate() noexcept
    {
        m_active = false;
    }

    DispatchSink m_sink;
    bool m_active = true;
};

template <typename Payload>
/// \brief Versioned application payload codec for one typed action route.
struct PayloadCodec {
    std::uint32_t version{}; ///< Persisted payload schema version.
    std::function<Result<std::string>(const Payload&)> encode; ///< Typed-to-bytes encoder.
    std::function<Result<Payload>(std::string_view)> decode; ///< Untrusted bytes decoder.
};

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
        m_entries.emplace(action_id.value, std::move(entry));

        ActionHandle<Payload> handle;
        handle.m_action_id = std::move(action_id);
        handle.m_payload_version = payload_version;
        handle.m_generation = generation;
        handle.m_owner = this;
        handle.m_registry_identity = m_identity;
        handle.m_encode = std::move(encode);
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
            if (found == m_entries.end() || found->second->payload_version != handle.payload_version()
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
        if (!handle.is_valid() || handle.m_owner != this
            || handle.m_registry_identity.lock() != m_identity) {
            return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
        }
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto found = m_entries.find(handle.action_id().value);
        if (found == m_entries.end()) {
            return Error{ErrorCode::UnknownAction, "action route is not registered"};
        }
        if (found->second->generation != handle.m_generation) {
            return Error{ErrorCode::InvalidArgument, "action handle is stale"};
        }
        if (found->second->payload_version != handle.payload_version()) {
            return Error{ErrorCode::PayloadVersionMismatch, "action payload version is incompatible"};
        }
        return Result<void>();
    }

private:
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

    const ITokenCodec& m_token_codec;
    const IClock& m_clock;
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Entry>> m_entries;
    std::uint64_t m_next_generation = 1;
    std::shared_ptr<const void> m_identity;
};

template <typename Payload>
Result<void> ActionHandle<Payload>::validate() const
{
    if (m_owner == nullptr || m_registry_identity.expired()) {
        return Error{ErrorCode::InvalidArgument, "action handle is invalid"};
    }
    return m_owner->validate_handle(*this);
}

} // namespace imtg

#endif // IMTG_ACTIONS_REGISTRY_HPP
