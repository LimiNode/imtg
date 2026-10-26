#include "imtg/core/core.hpp"

#include <chrono>
#include <functional>
#include <locale>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error("check failed: " #condition); \
        } \
    } while (false)

int main()
{
    const imtg::ConversationKey chat_key{
        imtg::BotId{7}, imtg::ChatId{-42}, imtg::Chat{}};
    const imtg::ConversationKey same_chat_key{
        imtg::BotId{7}, imtg::ChatId{-42}, imtg::Chat{}};
    const imtg::ConversationKey user_key{
        imtg::BotId{7}, imtg::ChatId{-42}, imtg::UserInChat{imtg::UserId{11}}};
    const imtg::ConversationKey other_bot_key{
        imtg::BotId{8}, imtg::ChatId{-42}, imtg::Chat{}};

    CHECK(chat_key == same_chat_key);
    CHECK(chat_key != user_key);
    CHECK(chat_key != other_bot_key);
    const std::hash<imtg::ConversationKey> key_hash;
    CHECK(key_hash(chat_key) == key_hash(same_chat_key));
    CHECK(key_hash(chat_key) != key_hash(user_key));
    CHECK(key_hash(chat_key) != key_hash(other_bot_key));
    CHECK(chat_key.encode() == "v1|b=7|c=-42|s=chat");
    CHECK(user_key.encode() == "v1|b=7|c=-42|s=user:11");

    const auto parsed_chat = imtg::parse_conversation_key(chat_key.encode());
    CHECK(parsed_chat);
    CHECK(parsed_chat.value() == chat_key);

    const auto parsed_user = imtg::parse_conversation_key(user_key.encode());
    CHECK(parsed_user);
    CHECK(parsed_user.value() == user_key);

    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-42|s=") );
    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-42") );
    CHECK(!imtg::parse_conversation_key("v1|b=x|c=-42|s=chat") );
    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-42|s=user:") );
    CHECK(!imtg::parse_conversation_key("v1|b=007|c=-42|s=chat"));
    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-042|s=chat"));
    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-42|s=user:011"));
    CHECK(!imtg::parse_conversation_key("v1|b=7|c=-42|s=chat|extra"));

    const imtg::ConversationKey boundary_key{
        imtg::BotId{std::numeric_limits<std::uint64_t>::max()},
        imtg::ChatId{std::numeric_limits<std::int64_t>::min()},
        imtg::Chat{}};
    CHECK(boundary_key.encode()
          == "v1|b=18446744073709551615|c=-9223372036854775808|s=chat");
    CHECK(key_hash(boundary_key) == key_hash(boundary_key));

    const auto old_locale = std::locale();
    std::locale::global(std::locale::classic());
    const auto canonical = chat_key.encode();
    try {
        std::locale::global(std::locale(""));
    } catch (const std::exception&) {
        // The host may not provide a user locale; classic is still deterministic.
    }
    CHECK(chat_key.encode() == canonical);
    std::locale::global(old_locale);

    std::unordered_set<imtg::ConversationKey> keys;
    keys.insert(chat_key);
    keys.insert(same_chat_key);
    keys.insert(user_key);
    CHECK(keys.size() == 2);
    CHECK(chat_key < other_bot_key);

    const auto initial = imtg::ClockTime{std::chrono::seconds{100}};
    imtg::ManualClock clock(initial);
    CHECK(clock.now() == initial);
    clock.advance(std::chrono::milliseconds{250});
    CHECK(clock.now() == initial + std::chrono::milliseconds{250});

    return 0;
}
