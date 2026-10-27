#ifndef IMTG_ACTIONS_TOKEN_HPP
#define IMTG_ACTIONS_TOKEN_HPP

#include "imtg/core/result.hpp"
#include "imtg/core/clock.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace imtg {

struct ActionId {
    std::string value;

    friend bool operator==(const ActionId& left, const ActionId& right) noexcept
    {
        return left.value == right.value;
    }
};

struct DecodedAction {
    ActionId action_id;
    std::uint32_t payload_version{};
    std::string payload;
    std::optional<ClockTime> expires_at;
};

/// \brief Boundary for encoding and validating external action tokens.
class ITokenCodec {
public:
    virtual ~ITokenCodec() = default;
    [[nodiscard]] virtual Result<std::string> encode(const DecodedAction& action) const = 0;
    [[nodiscard]] virtual Result<DecodedAction> decode(std::string_view token) const = 0;
};

/// \brief Deterministic test codec; it provides no authentication or integrity guarantee.
class PlainTokenCodec final : public ITokenCodec {
public:
    [[nodiscard]] Result<std::string> encode(const DecodedAction& action) const override;
    [[nodiscard]] Result<DecodedAction> decode(std::string_view token) const override;
};

} // namespace imtg

#endif // IMTG_ACTIONS_TOKEN_HPP
