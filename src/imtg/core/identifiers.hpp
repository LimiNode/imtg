#ifndef IMTG_CORE_IDENTIFIERS_HPP
#define IMTG_CORE_IDENTIFIERS_HPP

#include "imtg/core/result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace imtg {

struct BotId {
    std::uint64_t value{};
};

[[nodiscard]] constexpr bool operator==(BotId left, BotId right) noexcept
{
    return left.value == right.value;
}

struct ChatId {
    std::int64_t value{};
};

[[nodiscard]] constexpr bool operator==(ChatId left, ChatId right) noexcept
{
    return left.value == right.value;
}

struct UserId {
    std::uint64_t value{};
};

[[nodiscard]] constexpr bool operator==(UserId left, UserId right) noexcept
{
    return left.value == right.value;
}

struct Chat {
};

[[nodiscard]] constexpr bool operator==(Chat, Chat) noexcept
{
    return true;
}

struct UserInChat {
    UserId user_id;
};

[[nodiscard]] constexpr bool operator==(UserInChat left, UserInChat right) noexcept
{
    return left.user_id == right.user_id;
}

using SessionScope = std::variant<Chat, UserInChat>;

/// \brief Stable identity for one serialized logical interaction.
struct ConversationKey {
    BotId bot_id;
    ChatId chat_id;
    SessionScope session_scope;

    [[nodiscard]] std::string encode() const;
};

[[nodiscard]] bool operator==(const ConversationKey& left,
                              const ConversationKey& right) noexcept;

[[nodiscard]] bool operator!=(const ConversationKey& left,
                              const ConversationKey& right) noexcept;

[[nodiscard]] Result<ConversationKey> parse_conversation_key(std::string_view encoded);

[[nodiscard]] bool operator<(const ConversationKey& left, const ConversationKey& right);

} // namespace imtg

namespace std {

template <>
struct hash<imtg::BotId> {
    size_t operator()(imtg::BotId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <>
struct hash<imtg::ChatId> {
    size_t operator()(imtg::ChatId value) const noexcept
    {
        return hash<std::int64_t>{}(value.value);
    }
};

template <>
struct hash<imtg::UserId> {
    size_t operator()(imtg::UserId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <>
struct hash<imtg::ConversationKey> {
    size_t operator()(const imtg::ConversationKey& value) const noexcept
    {
        // Hash the typed identity directly.  In particular, do not call
        // ConversationKey::encode(): mailbox lookups run from a noexcept
        // worker loop and must not allocate.
        size_t seed = hash<imtg::BotId>{}(value.bot_id);
        const auto combine = [&seed](size_t component) noexcept {
            seed ^= component + static_cast<size_t>(0x9e3779b97f4a7c15ULL)
                + (seed << 6) + (seed >> 2);
        };
        combine(hash<imtg::ChatId>{}(value.chat_id));
        if (std::holds_alternative<imtg::Chat>(value.session_scope)) {
            combine(static_cast<size_t>(0));
        } else {
            combine(static_cast<size_t>(1));
            combine(hash<imtg::UserId>{}(std::get<imtg::UserInChat>(value.session_scope).user_id));
        }
        return seed;
    }
};

} // namespace std

#endif // IMTG_CORE_IDENTIFIERS_HPP
