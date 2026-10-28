#include "imtg/core/identifiers.hpp"

#include <charconv>
#include <iterator>
#include <limits>
#include <string>

namespace imtg {

namespace {

template <typename T>
Result<T> parse_integer(std::string_view value, ErrorCode code)
{
    if (value.empty()) {
        return Error{code, "integer field is empty"};
    }

    T result{};
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return Error{code, "integer field is invalid"};
    }
    return result;
}

} // namespace

std::string ConversationKey::encode() const
{
    std::string encoded = "v1|b=";
    const auto append_integer = [&encoded](auto value) {
        char buffer[std::numeric_limits<decltype(value)>::digits10 + 3];
        const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
        encoded.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
    };
    append_integer(bot_id.value);
    encoded += "|c=";
    append_integer(chat_id.value);
    encoded += "|s=";
    if (std::holds_alternative<Chat>(session_scope)) {
        encoded += "chat";
    } else {
        encoded += "user:";
        append_integer(std::get<UserInChat>(session_scope).user_id.value);
    }
    return encoded;
}

Result<ConversationKey> parse_conversation_key(std::string_view encoded)
{
    const std::string prefix = "v1|b=";
    if (encoded.size() < prefix.size() || encoded.substr(0, prefix.size()) != prefix) {
        return Error{ErrorCode::InvalidEncoding, "conversation key version is invalid"};
    }

    const auto chat_marker = encoded.find("|c=", prefix.size());
    const auto scope_marker = encoded.find("|s=", chat_marker == std::string_view::npos
        ? prefix.size() : chat_marker + 3);
    if (chat_marker == std::string_view::npos || scope_marker == std::string_view::npos
        || scope_marker <= chat_marker + 3) {
        return Error{ErrorCode::TruncatedEncoding, "conversation key fields are incomplete"};
    }

    const auto bot_value = parse_integer<std::uint64_t>(
        encoded.substr(prefix.size(), chat_marker - prefix.size()), ErrorCode::InvalidEncoding);
    if (!bot_value) {
        return bot_value.error();
    }

    const auto chat_value = parse_integer<std::int64_t>(
        encoded.substr(chat_marker + 3, scope_marker - chat_marker - 3),
        ErrorCode::InvalidEncoding);
    if (!chat_value) {
        return chat_value.error();
    }

    const auto scope = encoded.substr(scope_marker + 3);
    if (scope == "chat") {
        ConversationKey key{BotId{bot_value.value()}, ChatId{chat_value.value()}, Chat{}};
        if (key.encode() != encoded) {
            return Error{ErrorCode::InvalidEncoding, "conversation key is not canonical"};
        }
        return key;
    }

    constexpr std::string_view user_prefix = "user:";
    if (scope.substr(0, user_prefix.size()) == user_prefix) {
        const auto user_value = parse_integer<std::uint64_t>(
            scope.substr(user_prefix.size()), ErrorCode::InvalidEncoding);
        if (!user_value) {
            return user_value.error();
        }
        ConversationKey key{
            BotId{bot_value.value()},
            ChatId{chat_value.value()},
            UserInChat{UserId{user_value.value()}}};
        if (key.encode() != encoded) {
            return Error{ErrorCode::InvalidEncoding, "conversation key is not canonical"};
        }
        return key;
    }

    return Error{ErrorCode::InvalidEncoding, "conversation key scope is unknown"};
}

bool operator<(const ConversationKey& left, const ConversationKey& right)
{
    return left.encode() < right.encode();
}

bool operator==(const ConversationKey& left, const ConversationKey& right) noexcept
{
    return left.bot_id == right.bot_id && left.chat_id == right.chat_id
        && left.session_scope == right.session_scope;
}

bool operator!=(const ConversationKey& left, const ConversationKey& right) noexcept
{
    return !(left == right);
}

} // namespace imtg
