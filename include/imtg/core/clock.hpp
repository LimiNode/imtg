#ifndef IMTG_CORE_CLOCK_HPP
#define IMTG_CORE_CLOCK_HPP

#include <chrono>
#include <mutex>

namespace imtg {

using ClockTime = std::chrono::system_clock::time_point;

/// rief Clock boundary used by deterministic core components.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual ClockTime now() const = 0;
};

/// rief Production clock implementation backed by system_clock.
class SystemClock final : public IClock {
public:
    [[nodiscard]] ClockTime now() const override
    {
        return std::chrono::system_clock::now();
    }
};

/// rief Manually controlled clock for offline deterministic tests.
class ManualClock final : public IClock {
public:
    explicit ManualClock(ClockTime initial) : m_now(initial) {}

    [[nodiscard]] ClockTime now() const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_now;
    }

    void set(ClockTime value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_now = value;
    }

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
