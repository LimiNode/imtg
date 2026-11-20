#include <imtg/logging.hpp>

#include <logit.hpp>

#include "test_support.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class ThrowingLogger final : public logit::MemoryLogger {
public:
    void log(const logit::LogRecord&, const std::string&) override
    {
        throw std::runtime_error("untrusted backend failure");
    }
};

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
    adapter.record_runtime_shutdown(
        imtg::Result<void>(imtg::Error{imtg::ErrorCode::InvalidState,
                                       "cannot wait from worker"}));

    const auto messages = LOGIT_GET_BUFFERED_STRINGS(0);
    CHECK(messages.size() == 9);
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
    CHECK(contains("runtime.shutdown_failed"));
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

    // Identity disclosure is opt-in, including callbacks retained by value.
    LOGIT_ADD_MEMORY_LOGGER(4, 4096, 0);
    imtg::ReferenceRuntime::ErrorFunction retained;
    {
        imtg::logging::LogitAdapter private_adapter;
        retained = private_adapter.runtime_error_sink();
    }
    retained(key, imtg::Error{imtg::ErrorCode::RuntimeFailure, "secret"});
    const auto private_messages = LOGIT_GET_BUFFERED_STRINGS(2);
    CHECK(private_messages.size() == 1);
    CHECK(private_messages[0].find("bot_id") == std::string::npos);
    CHECK(private_messages[0].find("chat_id") == std::string::npos);
    CHECK(private_messages[0].find("secret") == std::string::npos);

    logit::Logger::get_instance().add_logger(
        std::make_unique<ThrowingLogger>(),
        std::make_unique<logit::SimpleLogFormatter>("%v"));
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec codec;
    imtg::ActionRegistry actions(codec, clock);
    std::atomic<int> completed{0};
    imtg::ReferenceRuntime runtime(actions, {}, {}, retained);
    CHECK(runtime.submit_callback(key, []() -> imtg::Result<void> {
        return imtg::Error{imtg::ErrorCode::RuntimeFailure, "secret task"};
    }));
    CHECK(runtime.submit_callback(key, [&completed]() -> imtg::Result<void> {
        ++completed;
        return {};
    }));
    CHECK(runtime.shutdown());
    CHECK(completed == 1);
    // imtg shutdown must not shut down the application-owned logger.
    logit::Logger::get_instance().set_logger_enabled(3, false);
    const auto before = LOGIT_GET_BUFFERED_STRINGS(0).size();
    adapter.record_runtime_shutdown(imtg::Result<void>());
    CHECK(LOGIT_GET_BUFFERED_STRINGS(0).size() == before + 1);
    return 0;
}
