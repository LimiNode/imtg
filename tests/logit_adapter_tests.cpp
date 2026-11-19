#include <imtg/logging.hpp>

#include <logit.hpp>

#include "test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

imtg::telegram::Update text_update(std::uint64_t update_id,
                                    std::string text)
{
    const imtg::ChatId chat{-100};
    return imtg::telegram::Update{
        imtg::BotId{7}, update_id,
        imtg::telegram::TextMessage{chat, imtg::UserId{42},
                                    imtg::telegram::MessageId{chat, 11},
                                    std::move(text)}};
}

} // namespace

int main()
{
    // MemoryLogger is synchronous and bounded, so this test does not depend
    // on global logger shutdown or on a background worker schedule.
    LOGIT_ADD_MEMORY_LOGGER(32, 4096, 0);
    LOGIT_SET_MAX_QUEUE(1);
    LOGIT_SET_QUEUE_POLICY(LOGIT_QUEUE_DROP_NEWEST);

    imtg::logging::LogitAdapter adapter(
        imtg::logging::LogitOptions{true});
    const auto update = text_update(11, "/secret private message");
    adapter.record_update_admission(imtg::telegram::UpdateOutcome::Accepted, update);
    adapter.record_update_admission(imtg::telegram::UpdateOutcome::Duplicate, update);
    adapter.record_update_admission(imtg::telegram::UpdateOutcome::QueueFull, update);

    const imtg::ConversationKey key{
        imtg::BotId{7}, imtg::ChatId{-100}, imtg::Chat{}};
    const imtg::telegram::UpdateContext context{
        update, key, imtg::telegram::HandlerMode::Headless};
    const auto sink = adapter.outcome_sink();
    sink(imtg::telegram::UpdateOutcome::Unauthorized, context,
         imtg::Error{imtg::ErrorCode::InvalidArgument, "secret error text"});
    sink(imtg::telegram::UpdateOutcome::Stale, context,
         imtg::Error{imtg::ErrorCode::StorageConflict, "private response body"});
    adapter.runtime_error_sink()(
        key, imtg::Error{imtg::ErrorCode::RuntimeFailure, "bot token"});
    adapter.record_runtime_shutdown(imtg::Result<void>());
    adapter.record_runtime_shutdown(
        imtg::Result<void>(imtg::Error{imtg::ErrorCode::RuntimeFailure,
                                       "untrusted exception"}));

    const auto messages = LOGIT_GET_BUFFERED_STRINGS(0);
    CHECK(messages.size() == 8);
    const auto contains = [&messages](const std::string& value) {
        return std::any_of(messages.begin(), messages.end(),
                           [&value](const std::string& message) {
                               return message.find(value) != std::string::npos;
                           });
    };
    CHECK(contains("telegram.update.accepted"));
    CHECK(contains("telegram.update.duplicate"));
    CHECK(contains("telegram.update.queue_full"));
    CHECK(contains("telegram.update.unauthorized"));
    CHECK(contains("telegram.update.stale"));
    CHECK(contains("runtime.task_failed"));
    CHECK(contains("runtime.shutdown"));
    CHECK(contains("runtime.fail_stop"));
    CHECK(!contains("secret"));
    CHECK(!contains("private"));
    CHECK(!contains("token"));
    CHECK(!contains("untrusted"));
    CHECK(contains("bot_id=7"));
    CHECK(contains("chat_id=-100"));
    CHECK(contains("update_id=11"));

    // Retention remains bounded even when the adapter is called repeatedly.
    LOGIT_ADD_MEMORY_LOGGER(2, 4096, 0);
    for (std::uint64_t id = 20; id < 30; ++id) {
        adapter.record_update_admission(
            imtg::telegram::UpdateOutcome::Rejected, text_update(id, "payload"));
    }
    CHECK(LOGIT_GET_BUFFERED_STRINGS(1).size() == 2);
    return 0;
}
