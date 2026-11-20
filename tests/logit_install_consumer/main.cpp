#include <imtg/runtime.hpp>
#include <logit.hpp>
#include <string>

int main()
{
    LOGIT_ADD_MEMORY_LOGGER(8, 2048, 0);
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec codec;
    imtg::ActionRegistry actions(codec, clock);
    {
        imtg::ReferenceRuntime runtime(actions, {}, {}, {});
        const imtg::ConversationKey key{imtg::BotId{7}, imtg::ChatId{-100}, imtg::Chat{}};
        if (!runtime.submit_callback(key, []() -> imtg::Result<void> {
                return imtg::Error{imtg::ErrorCode::RuntimeFailure, "secret body"};
            }) || !runtime.shutdown()) {
            return 1;
        }
    }
    const auto messages = LOGIT_GET_BUFFERED_STRINGS(0);
    if (messages.size() != 2 || messages[0].find("runtime.task_failed") == std::string::npos
        || messages[1].find("runtime.shutdown") == std::string::npos
        || messages[0].find("secret") != std::string::npos) {
        return 2;
    }
    return 0;
}
