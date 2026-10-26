#ifndef IMTG_CORE_RESULT_HPP
#define IMTG_CORE_RESULT_HPP

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace imtg {

/// \brief Stable error categories shared by core value-type APIs.
enum class ErrorCode {
    InvalidArgument,
    InvalidEncoding,
    TruncatedEncoding,
    UnsupportedVersion,
    DuplicateId,
    InvalidState,
    ScopeMismatch,
    UnknownAction,
    MalformedAction,
    PayloadVersionMismatch,
    ExpiredAction,
    HandlerUnavailable,
    DispatchRejected,
};

/// \brief Describes a failed core operation without requiring exceptions.
struct Error {
    ErrorCode code;
    std::string message;
};

template <typename T>
class Result {
public:
    Result(const T& value) : m_value(value) {}
    Result(T&& value) : m_value(std::move(value)) {}
    Result(const Error& error) : m_value(error) {}
    Result(Error&& error) : m_value(std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept
    {
        return std::holds_alternative<T>(m_value);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] const T& value() const&
    {
        return std::get<T>(m_value);
    }

    [[nodiscard]] T& value() &
    {
        return std::get<T>(m_value);
    }

    [[nodiscard]] T&& value() &&
    {
        return std::get<T>(std::move(m_value));
    }

    [[nodiscard]] const Error& error() const
    {
        return std::get<Error>(m_value);
    }

private:
    std::variant<T, Error> m_value;
};

template <>
class Result<void> {
public:
    Result() noexcept : m_error() {}
    Result(const Error& error) : m_error(error) {}
    Result(Error&& error) : m_error(std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept
    {
        return !m_error.has_value();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    [[nodiscard]] const Error& error() const
    {
        return *m_error;
    }

private:
    std::optional<Error> m_error;
};

} // namespace imtg

#endif // IMTG_CORE_RESULT_HPP
