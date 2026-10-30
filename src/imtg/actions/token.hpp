#ifndef IMTG_ACTIONS_TOKEN_HPP
#define IMTG_ACTIONS_TOKEN_HPP

#include "imtg/core/result.hpp"
#include "imtg/core/clock.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace imtg {

/// \brief Stable, versioned-route identity of an action handler.
struct ActionId {
    std::string value; ///< Canonical `name.vN` route.

    /// \brief Compares canonical route values.
    friend bool operator==(const ActionId& left, const ActionId& right) noexcept
    {
        return left.value == right.value;
    }
};

/// \brief Validated logical contents of one external action token.
struct DecodedAction {
    ActionId action_id; ///< Registered route identity.
    std::uint32_t payload_version{}; ///< Application payload schema version.
    std::string payload; ///< Codec-specific payload bytes.
    std::optional<ClockTime> expires_at; ///< Optional system-clock expiry boundary.
};

/// \brief Boundary for encoding and validating external action tokens.
class ITokenCodec {
public:
    virtual ~ITokenCodec() = default;
    /// \brief Encodes an action into an externally carried token.
    /// \param action Logical token contents.
    /// \return Encoded token or a validation/encoding error.
    [[nodiscard]] virtual Result<std::string> encode(const DecodedAction& action) const = 0;
    /// \brief Decodes and validates an externally carried token.
    /// \param token Untrusted token bytes.
    /// \return Logical contents or a decoding/integrity error.
    [[nodiscard]] virtual Result<DecodedAction> decode(std::string_view token) const = 0;
};

/// \brief Deterministic test codec; it provides no authentication or integrity guarantee.
class PlainTokenCodec final : public ITokenCodec {
public:
    /// \brief Deterministically encodes without authentication.
    /// \param action Logical token contents.
    /// \return Encoded token or a validation error.
    [[nodiscard]] Result<std::string> encode(const DecodedAction& action) const override;
    /// \brief Parses the deterministic unauthenticated format.
    /// \param token Untrusted token bytes.
    /// \return Logical contents or a decoding error.
    [[nodiscard]] Result<DecodedAction> decode(std::string_view token) const override;
};

} // namespace imtg

#endif // IMTG_ACTIONS_TOKEN_HPP
