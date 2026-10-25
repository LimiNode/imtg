#include "imtg/core/core.hpp"

#include <cassert>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_set>

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

    assert(chat_key == same_chat_key);
    assert(chat_key != user_key);
    assert(chat_key != other_bot_key);
    assert(chat_key.encode() == "v1|b=7|c=-42|s=chat");
    assert(user_key.encode() == "v1|b=7|c=-42|s=user:11");

    const auto parsed_chat = imtg::parse_conversation_key(chat_key.encode());
    assert(parsed_chat);
    assert(parsed_chat.value() == chat_key);

    const auto parsed_user = imtg::parse_conversation_key(user_key.encode());
    assert(parsed_user);
    assert(parsed_user.value() == user_key);

    assert(!imtg::parse_conversation_key("v1|b=7|c=-42|s=") );
    assert(!imtg::parse_conversation_key("v1|b=7|c=-42") );
    assert(!imtg::parse_conversation_key("v1|b=x|c=-42|s=chat") );
    assert(!imtg::parse_conversation_key("v1|b=7|c=-42|s=user:") );

    std::unordered_set<imtg::ConversationKey> keys;
    keys.insert(chat_key);
    keys.insert(same_chat_key);
    keys.insert(user_key);
    assert(keys.size() == 2);
    assert(chat_key < other_bot_key);

    const auto initial = imtg::ClockTime{std::chrono::seconds{100}};
    imtg::ManualClock clock(initial);
    assert(clock.now() == initial);
    clock.advance(std::chrono::milliseconds{250});
    assert(clock.now() == initial + std::chrono::milliseconds{250});

    return 0;
}
