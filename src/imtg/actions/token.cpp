#include "imtg/actions/token.hpp"

#include <charconv>
#include <chrono>

namespace imtg {

namespace {

template <typename T>
Result<T> parse_integer(std::string_view value)
{
    if (value.empty()) {
        return Error{ErrorCode::MalformedAction, "token integer is empty"};
    }
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return Error{ErrorCode::MalformedAction, "token integer is malformed"};
    }
    return result;
}

Result<std::pair<std::string_view, std::string_view>> read_field(
    std::string_view input,
    std::size_t& position)
{
    const auto separator = input.find(':', position);
    if (separator == std::string_view::npos) {
        return Error{ErrorCode::MalformedAction, "token field length is missing"};
    }
    const auto length_result = parse_integer<std::size_t>(input.substr(position, separator - position));
    if (!length_result) {
        return length_result.error();
    }
    const std::size_t value_begin = separator + 1;
    if (value_begin > input.size() || length_result.value() > input.size() - value_begin) {
        return Error{ErrorCode::MalformedAction, "token field is truncated"};
    }
    const std::size_t value_end = value_begin + length_result.value();
    const std::string_view value = input.substr(value_begin, length_result.value());
    position = value_end;
    if (position < input.size() && input[position] == '|') {
        ++position;
    } else if (position != input.size()) {
        return Error{ErrorCode::MalformedAction, "token field separator is invalid"};
    }
    return std::make_pair(input.substr(position, 0), value);
}

} // namespace

Result<std::string> PlainTokenCodec::encode(const DecodedAction& action) const
{
    if (action.action_id.value.empty()) {
        return Error{ErrorCode::InvalidArgument, "action id cannot be empty"};
    }
    const auto expiry = action.expires_at.has_value()
        ? std::chrono::duration_cast<std::chrono::milliseconds>(
              action.expires_at->time_since_epoch()).count()
        : -1;
    return "v1|" + std::to_string(action.action_id.value.size()) + ":"
        + action.action_id.value + "|" + std::to_string(action.payload_version) + "|"
        + std::to_string(action.payload.size()) + ":" + action.payload + "|"
        + std::to_string(expiry);
}

Result<DecodedAction> PlainTokenCodec::decode(std::string_view token) const
{
    constexpr std::string_view prefix = "v1|";
    if (token.substr(0, prefix.size()) != prefix) {
        return Error{ErrorCode::MalformedAction, "token version is invalid"};
    }

    std::size_t position = prefix.size();
    const auto action_field = read_field(token, position);
    if (!action_field) {
        return action_field.error();
    }
    if (position >= token.size()) {
        return Error{ErrorCode::MalformedAction, "token payload version is missing"};
    }
    const auto version_end = token.find('|', position);
    if (version_end == std::string_view::npos) {
        return Error{ErrorCode::MalformedAction, "token payload version is malformed"};
    }
    const auto version = parse_integer<std::uint32_t>(token.substr(position, version_end - position));
    if (!version) {
        return version.error();
    }
    position = version_end + 1;

    const auto payload_field = read_field(token, position);
    if (!payload_field) {
        return payload_field.error();
    }
    if (position >= token.size()) {
        return Error{ErrorCode::MalformedAction, "token expiry is missing"};
    }
    const auto expiry = parse_integer<std::int64_t>(token.substr(position));
    if (!expiry) {
        return expiry.error();
    }

    std::optional<ClockTime> expires_at;
    if (expiry.value() >= 0) {
        expires_at = ClockTime{std::chrono::milliseconds{expiry.value()}};
    }
    return DecodedAction{
        ActionId{std::string(action_field.value().second)},
        version.value(),
        std::string(payload_field.value().second),
        expires_at};
}

} // namespace imtg
