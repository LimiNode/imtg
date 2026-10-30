#ifndef IMTG_CORE_CLOCK_HPP
#define IMTG_CORE_CLOCK_HPP

#include <chrono>
#include <mutex>

namespace imtg {

using ClockTime = std::chrono::system_clock::time_point;

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
    void advance(std::chrono::milliseconds amount)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_now += amount;
    }

private:
    mutable std::mutex m_mutex;
    ClockTime m_now;
};

} // namespace imtg

#endif // IMTG_CORE_CLOCK_HPP
