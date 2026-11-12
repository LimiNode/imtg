#include <imtg/storage.hpp>
#include <limits>
#include <ratio>
#include <type_traits>

namespace imtg {
namespace {
Error range_error() { return {ErrorCode::InvalidEncoding, "timestamp is outside the supported range"}; }
using Rep = ClockTime::duration::rep;
using Scale = std::ratio_divide<ClockTime::duration::period, std::milli>;
static_assert(std::is_integral<Rep>::value && std::is_signed<Rep>::value && sizeof(Rep) <= 8,
              "storage timestamps require a signed integral system clock of at most 64 bits");
}

Result<std::int64_t> encode_storage_timestamp(ClockTime value)
{
    const auto ticks = static_cast<std::int64_t>(value.time_since_epoch().count());
    if constexpr (Scale::num == 1) {
        const auto millis = ticks / Scale::den;
        return millis + (ticks % Scale::den > 0 ? 1 : 0);
    } else if constexpr (Scale::den == 1) {
        if (ticks > std::numeric_limits<std::int64_t>::max() / Scale::num
            || ticks < std::numeric_limits<std::int64_t>::min() / Scale::num) return range_error();
        return ticks * Scale::num;
    } else {
        return range_error();
    }
}

Result<ClockTime> decode_storage_timestamp(std::uint64_t raw)
{
    const std::int64_t millis = raw <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? static_cast<std::int64_t>(raw) : -1 - static_cast<std::int64_t>(~raw);
    std::int64_t ticks{};
    if constexpr (Scale::num == 1) {
        if (millis > std::numeric_limits<Rep>::max() / Scale::den
            || millis < std::numeric_limits<Rep>::min() / Scale::den) return range_error();
        ticks = millis * Scale::den;
    } else if constexpr (Scale::den == 1) {
        if (millis % Scale::num != 0) return range_error();
        ticks = millis / Scale::num;
        if (ticks > std::numeric_limits<Rep>::max() || ticks < std::numeric_limits<Rep>::min())
            return range_error();
    } else {
        return range_error();
    }
    return ClockTime{ClockTime::duration{static_cast<Rep>(ticks)}};
}
} // namespace imtg
