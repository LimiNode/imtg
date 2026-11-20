#include <imtg/runtime.hpp>

#ifdef IMTG_TEST_TELEGRAM
#include <imtg/telegram.hpp>
#endif
#include <logit.hpp>
#include "test_support.hpp"
#include <algorithm>
#include <atomic>
#include <future>
#include <stdexcept>

namespace {
class ThrowingLogger final : public logit::MemoryLogger {
public:
    void log(const logit::LogRecord&, const std::string&) override
    {
        throw std::runtime_error("untrusted backend failure");
    }
};
class ReentrantLogger final : public logit::MemoryLogger {
public:
    std::atomic<imtg::ReferenceRuntime*> runtime{nullptr};
    std::atomic<int> calls{0};
    void log(const logit::LogRecord& record, const std::string& message) override
    {
        if (auto* active = runtime.load()) {
            (void)active->failure_status();
            ++calls;
        }
        logit::MemoryLogger::log(record, message);
    }
};
}

int main()
{
    LOGIT_ADD_MEMORY_LOGGER(64, 4096, 0);
    LOGIT_SET_MAX_QUEUE(1);
    LOGIT_SET_QUEUE_POLICY(LOGIT_QUEUE_DROP_NEWEST);
    auto reentrant = std::make_unique<ReentrantLogger>();
    auto* probe = reentrant.get();
    logit::Logger::get_instance().add_logger(std::move(reentrant),
        std::make_unique<logit::SimpleLogFormatter>("%v"));
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec codec;
    imtg::ActionRegistry actions(codec, clock);
    const imtg::ConversationKey key{imtg::BotId{7}, imtg::ChatId{-100}, imtg::Chat{}};
    std::atomic<int> completed{0};
    std::atomic<int> observed{0};
    const auto error_sink = [&observed](const auto&, const auto&) { ++observed; };
    {
        imtg::ReferenceRuntime runtime(actions, {}, {}, error_sink);
        probe->runtime = &runtime;
        CHECK(runtime.submit_callback(key, []() -> imtg::Result<void> {
            return imtg::Error{imtg::ErrorCode::RuntimeFailure, "secret task token"};
        }));
        CHECK(runtime.submit_callback(key, [&completed] { ++completed; return imtg::Result<void>(); }));
        CHECK(runtime.shutdown());
        CHECK(!runtime.submit_callback(key, [] { return imtg::Result<void>(); }));
        CHECK(runtime.shutdown());
        probe->runtime = nullptr;
    }
    CHECK(completed == 1);
    CHECK(observed == 1);
    CHECK(probe->calls >= 3);
    const auto count = [](const std::string& code) {
        const auto messages = LOGIT_GET_BUFFERED_STRINGS(0);
        return std::count_if(messages.begin(), messages.end(), [&code](const auto& message) {
            return message.find(code) != std::string::npos;
        });
    };
    CHECK(count("runtime.task_failed") == 1);
    CHECK(count("runtime.shutdown") == 1);
    CHECK(count("runtime.admission_rejected") == 1);
    {
        imtg::ReferenceRuntime runtime(actions, {}, {}, {});
        CHECK(runtime.submit_callback(key, [&runtime] {
            CHECK(!runtime.shutdown());
            return imtg::Result<void>();
        }));
        CHECK(runtime.shutdown());
        CHECK(count("runtime.shutdown_rejected") == 1);
    }
    {
        auto config = imtg::RuntimeConfig{};
        config.fail_queue_insertion_on_submit = 0;
        imtg::ReferenceRuntime runtime(actions, {}, {}, {}, config);
        probe->runtime = &runtime;
        const auto accepted = runtime.submit_callback(key, [] { return imtg::Result<void>(); });
        CHECK(!accepted);
        CHECK(accepted.error().code == imtg::ErrorCode::QueueFull);
        CHECK(runtime.shutdown());
        probe->runtime = nullptr;
    }
    {
        auto config = imtg::RuntimeConfig{};
        config.fail_worker_start_after = 0;
        bool threw = false;
        try { imtg::ReferenceRuntime runtime(actions, {}, {}, {}, config); }
        catch (...) { threw = true; }
        CHECK(threw);
        CHECK(count("runtime.startup_failed") == 1);
    }
    {
        auto config = imtg::RuntimeConfig{};
        config.worker_count = 1;
        config.fail_ready_requeue = true;
        imtg::ReferenceRuntime runtime(actions, {}, {}, {}, config);
        probe->runtime = &runtime;
        std::promise<void> release;
        const auto gate = release.get_future().share();
        CHECK(runtime.submit_callback(key, [gate] { gate.wait(); return imtg::Result<void>(); }));
        CHECK(runtime.submit_callback(key, [] { return imtg::Result<void>(); }));
        release.set_value();
        CHECK(!runtime.shutdown());
        CHECK(runtime.discarded_task_count() == 1);
        probe->runtime = nullptr;
        CHECK(count("runtime.fail_stop") == 1);
        CHECK(count("runtime.shutdown_failed") == 1);
    }
#ifdef IMTG_TEST_TELEGRAM
    {
        imtg::ReferenceRuntime runtime(actions, {}, {}, error_sink);
        std::atomic<int> outcomes{0};
        imtg::telegram::UpdateRouter router(runtime, {},
            [&outcomes](const auto&, const auto&, const auto&) { ++outcomes; });
        imtg::telegram::Route route;
        route.kind = imtg::telegram::UpdateKind::TextMessage;
        route.handler = [](const auto&) -> imtg::Result<void> {
            return imtg::Error{imtg::ErrorCode::HandlerUnavailable, "private response body"};
        };
        CHECK(router.add_route(std::move(route)));
        const imtg::ChatId chat{-100};
        const imtg::telegram::Update update{imtg::BotId{7}, 11,
            imtg::telegram::TextMessage{chat, imtg::UserId{42},
                imtg::telegram::MessageId{chat, 11}, "secret personal text"}};
        CHECK(router.receive(update) == imtg::telegram::UpdateOutcome::Accepted);
        CHECK(router.receive(update) == imtg::telegram::UpdateOutcome::Duplicate);
        auto invalid = update;
        invalid.bot_id.value = 0;
        CHECK(router.receive(invalid) == imtg::telegram::UpdateOutcome::Invalid);
        router.close();
        router.close();
        auto fresh = update;
        ++fresh.update_id;
        CHECK(router.receive(fresh) == imtg::telegram::UpdateOutcome::Closed);
        CHECK(runtime.shutdown());
        CHECK(outcomes == 1);
        CHECK(observed == 2);
        CHECK(count("telegram.update.admission") == 4);
        CHECK(count("telegram.router.closed") == 1);
        CHECK(count("runtime.task_failed") == 2);
    }
#endif
    logit::Logger::get_instance().add_logger(std::make_unique<ThrowingLogger>(),
        std::make_unique<logit::SimpleLogFormatter>("%v"));
    {
        imtg::ReferenceRuntime runtime(actions, {}, {}, error_sink);
        CHECK(runtime.submit_callback(key, []() -> imtg::Result<void> {
            return imtg::Error{imtg::ErrorCode::RuntimeFailure, "untrusted exception"};
        }));
        CHECK(runtime.submit_callback(key, [&completed] { ++completed; return imtg::Result<void>(); }));
        CHECK(runtime.shutdown());
    }
    CHECK(completed == 2);
    logit::Logger::get_instance().set_logger_enabled(2, false);
    const auto before = LOGIT_GET_BUFFERED_STRINGS(0).size();
    LOGIT_INFO("application.after_imtg_shutdown");
    CHECK(LOGIT_GET_BUFFERED_STRINGS(0).size() == before + 1);
    for (const auto& message : LOGIT_GET_BUFFERED_STRINGS(0)) {
        CHECK(message.find("secret") == std::string::npos);
        CHECK(message.find("private") == std::string::npos);
        CHECK(message.find("token") == std::string::npos);
        CHECK(message.find("untrusted") == std::string::npos);
        CHECK(message.find("bot_id") == std::string::npos);
        CHECK(message.find("chat_id") == std::string::npos);
    }
    LOGIT_ADD_MEMORY_LOGGER(2, 4096, 0);
    for (int index = 0; index < 8; ++index) {
        imtg::ReferenceRuntime runtime(actions, {}, {}, {});
        CHECK(runtime.shutdown());
    }
    CHECK(LOGIT_GET_BUFFERED_STRINGS(3).size() == 2);
    return 0;
}
