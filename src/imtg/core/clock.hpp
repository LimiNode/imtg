#ifndef IMTG_CORE_CLOCK_HPP
#define IMTG_CORE_CLOCK_HPP

/// \file imtg/core/clock.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include <chrono>
#include <mutex>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace imtg {

/// \brief System-clock-domain timestamp used by deadlines and persisted time encodings.
using ClockTime = std::chrono::system_clock::time_point;

namespace detail {

/// \brief Checks integral duration scaling for the supported whole-factor clock periods.
/// \tparam To Destination duration with a signed integral representation of at most 64 bits.
/// \tparam Rep Source signed integral representation of at most 64 bits.
/// \tparam Period Source tick period, related to the destination by a whole factor.
/// \param value Source duration with a signed integral representation of at most 64 bits.
/// \return Truncated duration, or empty if its count cannot be represented.
template <typename To, typename Rep, typename Period>
std::optional<To> checked_duration_cast(std::chrono::duration<Rep, Period> value) noexcept
{
    using TargetRep = typename To::rep;
    using Scale = std::ratio_divide<Period, typename To::period>;
    static_assert(std::is_integral<Rep>::value && std::is_signed<Rep>::value
        && std::is_integral<TargetRep>::value && std::is_signed<TargetRep>::value
        && sizeof(Rep) <= sizeof(std::intmax_t) && sizeof(TargetRep) <= sizeof(std::intmax_t),
        "checked clock durations require signed integral counts of at most 64 bits");
    static_assert(Scale::num == 1 || Scale::den == 1,
        "supported clock periods must divide each other by a whole factor");
    auto count = static_cast<std::intmax_t>(value.count());
    constexpr auto low = static_cast<std::intmax_t>((std::numeric_limits<TargetRep>::min)());
    constexpr auto high = static_cast<std::intmax_t>((std::numeric_limits<TargetRep>::max)());
    if constexpr (Scale::num == 1) {
        count /= Scale::den;
        if (count < low || count > high) return std::nullopt;
    } else {
        if (count < low / Scale::num || count > high / Scale::num) return std::nullopt;
        count *= Scale::num;
    }
    return To{static_cast<TargetRep>(count)};
}

} // namespace detail

/// \brief Clock boundary used by deterministic core components.
class IClock {
public:
    virtual ~IClock() = default;
    /// \brief Returns the current system-clock-domain time.
    /// \return Current time used for expiry and deadline decisions.
    /// \thread_safety Implementations must make concurrent calls safe.
    [[nodiscard]] virtual ClockTime now() const = 0;
};

/// \brief Production clock implementation backed by system_clock.
class SystemClock final : public IClock {
public:
    /// \brief Returns `std::chrono::system_clock::now()`.
    /// \return Current wall-clock time.
    [[nodiscard]] ClockTime now() const override
    {
        return std::chrono::system_clock::now();
    }
};

/// \brief Manually controlled clock for offline deterministic tests.
class ManualClock final : public IClock {
public:
    /// \brief Creates a clock at a caller-selected instant.
    /// \param initial Initial system-clock-domain value.
    explicit ManualClock(ClockTime initial) : m_now(initial) {}

    /// \brief Returns the current manually controlled value.
    /// \return Current test-clock value.
    /// \thread_safety Safe to call concurrently with `set()` and `advance()`.
    [[nodiscard]] ClockTime now() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_now;
    }

    /// \brief Replaces the current value.
    /// \param value New system-clock-domain value.
    void set(ClockTime value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_now = value;
    }

    /// \brief Advances the current value by an explicit duration.
    /// \param amount Duration to add; negative values move the clock backwards.
    /// \throws std::overflow_error If conversion or addition is out of range;
    /// the clock is unchanged. Sub-tick durations truncate toward zero.
    void advance(std::chrono::milliseconds amount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        using Duration = ClockTime::duration;
        using Rep = Duration::rep;
        const auto delta = detail::checked_duration_cast<Duration>(amount);
        if (!delta) throw std::overflow_error("manual clock duration is out of range");
        const auto ticks = m_now.time_since_epoch().count();
        const auto offset = delta->count();
        if ((offset > 0 && ticks > (std::numeric_limits<Rep>::max)() - offset)
            || (offset < 0 && ticks < (std::numeric_limits<Rep>::min)() - offset)) {
            throw std::overflow_error("manual clock addition is out of range");
        }
        m_now = ClockTime{Duration{ticks + offset}};
    }

private:
    mutable std::mutex m_mutex;
    ClockTime m_now;
};

} // namespace imtg

#endif // IMTG_CORE_CLOCK_HPP
