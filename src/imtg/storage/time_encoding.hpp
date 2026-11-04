#ifndef IMTG_STORAGE_TIME_ENCODING_HPP
#define IMTG_STORAGE_TIME_ENCODING_HPP

#include "imtg/core/clock.hpp"
#include "imtg/core/result.hpp"
#include <cstdint>

namespace imtg {
/// \brief Converts a system-clock timestamp to signed milliseconds, rounding toward the future.
/// \param value Timestamp in the authoritative clock domain.
/// \return Milliseconds or InvalidEncoding if the wire range cannot represent the timestamp.
[[nodiscard]] Result<std::int64_t> encode_storage_timestamp(ClockTime value);

/// \brief Converts portable two's-complement millisecond wire bits to a checked clock timestamp.
/// \param raw Unsigned bits read from a little-endian signed 64-bit field.
/// \return Timestamp or InvalidEncoding if the clock cannot represent it exactly.
[[nodiscard]] Result<ClockTime> decode_storage_timestamp(std::uint64_t raw);
} // namespace imtg
#endif
