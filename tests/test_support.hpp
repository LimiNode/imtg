#ifndef IMTG_TEST_SUPPORT_HPP
#define IMTG_TEST_SUPPORT_HPP

#include <chrono>
#include <stdexcept>

#define CHECK(condition) \
    do { \
        if (!(condition)) throw std::runtime_error("check failed: " #condition); \
    } while (false)

template <typename ConditionVariable, typename Lock, typename Predicate>
bool wait_bounded(ConditionVariable& condition,
                  Lock& lock,
                  Predicate&& predicate,
                  std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    return condition.wait_for(lock, timeout, static_cast<Predicate&&>(predicate));
}

#endif // IMTG_TEST_SUPPORT_HPP
