#ifndef IMTG_CORE_IDENTIFIERS_HPP
#define IMTG_CORE_IDENTIFIERS_HPP

#include "imtg/core/result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>

namespace imtg {

/// \brief Stable unsigned identity of one bot configuration.
struct BotId {
    std::uint64_t value{}; ///< Source bot identifier.
};

/// \brief Compares bot identities.
[[nodiscard]] constexpr bool operator==(BotId left, BotId right) noexcept
{
    return left.value == right.value;
}

/// \brief Signed Telegram-compatible chat identity.
struct ChatId {
    std::int64_t value{}; ///< Source chat identifier.
};

/// \brief Compares chat identities.
[[nodiscard]] constexpr bool operator==(ChatId left, ChatId right) noexcept
{
    return left.value == right.value;
}

/// \brief Stable unsigned identity of one user.
struct UserId {
    std::uint64_t value{}; ///< Source user identifier.
};

/// \brief Compares user identities.
[[nodiscard]] constexpr bool operator==(UserId left, UserId right) noexcept
{
    return left.value == right.value;
}

/// \brief Session scope shared by all users in a chat.
struct Chat {
};

/// \brief Compares chat-wide scope markers; all instances are equivalent.
[[nodiscard]] constexpr bool operator==(Chat, Chat) noexcept
{
    return true;
}

/// \brief Session scope isolated to one user within a chat.
struct UserInChat {
    UserId user_id; ///< User owning the scoped session.
};

/// \brief Compares user-in-chat scopes.
[[nodiscard]] constexpr bool operator==(UserInChat left, UserInChat right) noexcept
{
    return left.user_id == right.user_id;
}

using SessionScope = std::variant<Chat, UserInChat>; ///< Chat-wide or user-in-chat isolation.

/// \brief Stable identity for one serialized logical interaction.
struct ConversationKey {
    BotId bot_id; ///< Bot namespace owning the conversation.
    ChatId chat_id; ///< Chat containing the interaction.
    SessionScope session_scope; ///< Chat-wide or user-specific isolation.

    /// \brief Encodes the typed identity into its canonical stable representation.
    /// \return Binary-safe string suitable for persistence and diagnostics.
    [[nodiscard]] std::string encode() const;
};

/// \brief Compares all typed components of two conversation identities.
[[nodiscard]] bool operator==(const ConversationKey& left,
                              const ConversationKey& right) noexcept;

/// \brief Reports whether conversation identities differ.
[[nodiscard]] bool operator!=(const ConversationKey& left,
                              const ConversationKey& right) noexcept;

/// \brief Parses and validates a canonical conversation-key encoding.
/// \param encoded Encoding produced by `ConversationKey::encode()`.
/// \return Typed identity or an encoding error.
[[nodiscard]] Result<ConversationKey> parse_conversation_key(std::string_view encoded);

/// \brief Provides deterministic lexicographic ordering by typed components.
[[nodiscard]] bool operator<(const ConversationKey& left, const ConversationKey& right);

} // namespace imtg

namespace std {

/// \brief Standard hash support for `BotId`.
template <>
struct hash<imtg::BotId> {
    /// \brief Hashes the typed numeric field without allocation.
    /// \param value Bot identity to hash.
    /// \return Hash value suitable for unordered containers.
    size_t operator()(imtg::BotId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

/// \brief Standard hash support for `ChatId`.
template <>
struct hash<imtg::ChatId> {
    /// \brief Hashes the typed numeric field without allocation.
    /// \param value Chat identity to hash.
    /// \return Hash value suitable for unordered containers.
    size_t operator()(imtg::ChatId value) const noexcept
    {
        return hash<std::int64_t>{}(value.value);
    }
};

/// \brief Standard hash support for `UserId`.
template <>
struct hash<imtg::UserId> {
    /// \brief Hashes the typed numeric field without allocation.
    /// \param value User identity to hash.
    /// \return Hash value suitable for unordered containers.
    size_t operator()(imtg::UserId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

/// \brief Allocation-free standard hash support for `ConversationKey`.
template <>
struct hash<imtg::ConversationKey> {
    /// \brief Hashes typed identity fields without calling `encode()`.
    /// \param value Conversation identity to hash.
    /// \return Allocation-free hash value suitable for unordered containers.
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
