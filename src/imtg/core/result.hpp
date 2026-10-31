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
    QueueFull,
    RuntimeFailure,
    StorageNotFound,
    StorageConflict,
    StorageReadOnly,
    StorageStaleClaim,
    StorageUnavailable,
    StorageSchemaFailure,
    StorageSerializationFailure,
    StorageRecordTooLarge,
    StoragePermissionDenied,
    StorageUnknownCommit,
    StorageRevisionExhausted,
};

/// \brief Describes a failed core operation without requiring exceptions.
struct Error {
    ErrorCode code; ///< Stable category suitable for programmatic handling.
    std::string message; ///< Human-readable diagnostic; not a stable protocol value.
};

/// \brief Exception-free success-or-error value.
/// \tparam T Success value type.
template <typename T>
class Result {
public:
    /// \brief Constructs a successful result by copying a value.
    /// \param value Success value.
    Result(const T& value) : m_value(value) {}
    /// \brief Constructs a successful result by moving a value.
    /// \param value Success value.
    Result(T&& value) : m_value(std::move(value)) {}
    /// \brief Constructs a failed result by copying an error.
    /// \param error Failure diagnostic.
    Result(const Error& error) : m_value(error) {}
    /// \brief Constructs a failed result by moving an error.
    /// \param error Failure diagnostic.
    Result(Error&& error) : m_value(std::move(error)) {}

    /// \brief Reports whether this result contains a success value.
    /// \return `true` when a value is present.
    [[nodiscard]] bool has_value() const noexcept
    {
        return std::holds_alternative<T>(m_value);
    }

    /// \brief Converts to `true` for success and `false` for failure.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /// \brief Returns the success value by const reference.
    /// \return Stored success value.
    /// \pre `has_value()` is true.
    [[nodiscard]] const T& value() const&
    {
        return std::get<T>(m_value);
    }

    /// \brief Returns the success value by mutable reference.
    /// \return Stored success value.
    /// \pre `has_value()` is true.
    [[nodiscard]] T& value() &
    {
        return std::get<T>(m_value);
    }

    /// \brief Moves the success value out of an rvalue result.
    /// \return Stored success value as an rvalue reference.
    /// \pre `has_value()` is true.
    [[nodiscard]] T&& value() &&
    {
        return std::get<T>(std::move(m_value));
    }

    /// \brief Returns the failure diagnostic.
    /// \return Stored error.
    /// \pre `has_value()` is false.
    [[nodiscard]] const Error& error() const
    {
        return std::get<Error>(m_value);
    }

private:
    std::variant<T, Error> m_value;
};

/// \brief Exception-free success-or-error result for operations without a value.
template <>
class Result<void> {
public:
    /// \brief Constructs a successful result.
    Result() noexcept : m_error() {}
    /// \brief Constructs a failed result by copying an error.
    /// \param error Failure diagnostic.
    Result(const Error& error) : m_error(error) {}
    /// \brief Constructs a failed result by moving an error.
    /// \param error Failure diagnostic.
    Result(Error&& error) : m_error(std::move(error)) {}

    /// \brief Reports whether the operation succeeded.
    /// \return `true` when no error is stored.
    [[nodiscard]] bool has_value() const noexcept
    {
        return !m_error.has_value();
    }

    /// \brief Converts to `true` for success and `false` for failure.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return has_value();
    }

    /// \brief Returns the failure diagnostic.
    /// \return Stored error.
    /// \pre `has_value()` is false.
    [[nodiscard]] const Error& error() const
    {
        return *m_error;
    }

private:
    std::optional<Error> m_error;
};

} // namespace imtg

#endif // IMTG_CORE_RESULT_HPP
