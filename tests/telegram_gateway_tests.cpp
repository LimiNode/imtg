#include <imtg/telegram.hpp>

#include "test_support.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

imtg::telegram::Update text_update(std::uint64_t update_id,
                                   std::uint64_t bot_id,
                                   std::int64_t chat_id,
                                   std::uint64_t user_id,
                                   std::string text)
{
    const imtg::ChatId chat{chat_id};
    return imtg::telegram::Update{
        imtg::BotId{bot_id}, update_id,
        imtg::telegram::TextMessage{chat, imtg::UserId{user_id},
                                    imtg::telegram::MessageId{chat, 11}, std::move(text)}};
}

imtg::telegram::Update callback_update(std::uint64_t update_id,
                                       std::uint64_t bot_id,
                                       std::int64_t chat_id,
                                       std::uint64_t user_id,
                                       std::string data)
{
    const imtg::ChatId chat{chat_id};
    return imtg::telegram::Update{
        imtg::BotId{bot_id}, update_id,
        imtg::telegram::CallbackQuery{"callback-" + std::to_string(update_id),
                                      chat, imtg::UserId{user_id},
                                      imtg::telegram::MessageId{chat, 11}, std::move(data)}};
}

imtg::RuntimeConfig test_runtime_config(std::size_t queue_capacity = 8)
{
    imtg::RuntimeConfig config;
    config.worker_count = 1;
    config.max_active_mailboxes = 8;
    config.max_queued_tasks_total = 16;
    config.max_queued_tasks_per_conversation = queue_capacity;
    return config;
}

struct FakeTransport {
    imtg::telegram::UpdateRouter& router;

    imtg::telegram::UpdateOutcome deliver(imtg::telegram::Update update)
    {
        return router.receive(std::move(update));
    }
};

} // namespace

int main()
{
    imtg::ManualClock clock(imtg::ClockTime{std::chrono::seconds{1}});
    imtg::PlainTokenCodec codec;
    imtg::ActionRegistry actions(codec, clock);
    std::mutex mutex;
    std::condition_variable condition;
    std::size_t handled = 0;
    std::size_t render_calls = 0;
    std::vector<imtg::telegram::UpdateOutcome> outcomes;

    imtg::ReferenceRuntime runtime(
        actions,
        [&render_calls](const imtg::ConversationKey&) -> imtg::Result<imtg::UiTree> {
            ++render_calls;
            return imtg::Error{imtg::ErrorCode::InvalidState, "callback task rendered unexpectedly"};
        }, {},
        [&mutex, &condition](const imtg::ConversationKey&, const imtg::Error&) {
            condition.notify_all();
        },
        test_runtime_config());
    imtg::telegram::RouterConfig router_config;
    router_config.bot_username = "demo";
    imtg::telegram::UpdateRouter router(
        runtime, router_config,
        [&mutex, &condition, &outcomes](imtg::telegram::UpdateOutcome outcome,
                                        const imtg::telegram::UpdateContext&,
                                        const imtg::Error&) {
            std::lock_guard<std::mutex> lock(mutex);
            outcomes.push_back(outcome);
            condition.notify_all();
        });

    CHECK(router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::BotCommand, "start", {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&mutex, &condition, &handled](const imtg::telegram::UpdateContext& context) {
            CHECK(context.kind() == imtg::telegram::UpdateKind::BotCommand);
            CHECK((context.conversation == imtg::ConversationKey{
                imtg::BotId{7}, imtg::ChatId{-100}, imtg::Chat{}}));
            std::lock_guard<std::mutex> lock(mutex);
            ++handled;
            condition.notify_all();
            return imtg::Result<void>();
        }}));
    CHECK(router.add_middleware([](const imtg::telegram::UpdateContext&) {
        return imtg::Result<void>();
    }));
    FakeTransport transport{router};

    auto start = text_update(1, 7, -100, 42, "/start@demo");
    CHECK(transport.deliver(start) == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(transport.deliver(start) == imtg::telegram::UpdateOutcome::Duplicate);
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(wait_bounded(condition, lock, [&handled] { return handled == 1; }));
    }
    CHECK(outcomes.empty());
    CHECK(render_calls == 0);

    CHECK(router.receive(text_update(13, 7, -100, 42, "/start@another_bot"))
          == imtg::telegram::UpdateOutcome::Rejected);
    CHECK(router.receive(text_update(14, 7, -100, 42, "/start@"))
          == imtg::telegram::UpdateOutcome::Invalid);
    CHECK(router.receive(text_update(15, 7, -100, 42,
                                    "/123456789012345678901234567890123"))
          == imtg::telegram::UpdateOutcome::Invalid);
    CHECK(router.receive(text_update(16, 7, -100, 42, "/START"))
          == imtg::telegram::UpdateOutcome::Invalid);
    CHECK(router.receive(text_update(17, 7, -100, 42, "/"))
          == imtg::telegram::UpdateOutcome::Invalid);

    imtg::telegram::UpdateRouter invalid_route_router(runtime);
    CHECK(!invalid_route_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::BotCommand,
        "not-valid-command!", {}, imtg::telegram::HandlerMode::Headless,
        {}, {}, [](const imtg::telegram::UpdateContext&) {
            return imtg::Result<void>();
        }}));

    auto unknown = text_update(2, 7, -100, 42, "/unknown");
    CHECK(router.receive(std::move(unknown)) == imtg::telegram::UpdateOutcome::Rejected);

    auto malformed = text_update(0, 7, -100, 42, "/start");
    CHECK(router.receive(std::move(malformed)) == imtg::telegram::UpdateOutcome::Invalid);

    imtg::telegram::RouterConfig small_config;
    small_config.max_text_bytes = 3;
    imtg::telegram::UpdateRouter small_router(runtime, small_config);
    CHECK(small_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [](const imtg::telegram::UpdateContext&) { return imtg::Result<void>(); }}));
    CHECK(small_router.receive(text_update(20, 7, -100, 42, "long"))
          == imtg::telegram::UpdateOutcome::Invalid);

    imtg::telegram::UpdateRouter callback_router(
        runtime, {},
        [&mutex, &condition, &outcomes](imtg::telegram::UpdateOutcome outcome,
                                        const imtg::telegram::UpdateContext&,
                                        const imtg::Error&) {
            std::lock_guard<std::mutex> lock(mutex);
            outcomes.push_back(outcome);
            condition.notify_all();
        });
    imtg::telegram::Route save_route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "save",
        imtg::telegram::HandlerMode::Headless, {}, {},
        [](const imtg::telegram::UpdateContext&) { return imtg::Result<void>(); }};
    save_route.binding.bot_id = imtg::BotId{7};
    save_route.binding.chat_id = imtg::ChatId{-100};
    save_route.binding.user_id = imtg::UserId{42};
    save_route.binding.message_id = imtg::telegram::MessageId{imtg::ChatId{-100}, 11};
    CHECK(callback_router.add_route(std::move(save_route)));
    CHECK(callback_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "stale",
        imtg::telegram::HandlerMode::Interactive, {},
        [](const imtg::telegram::UpdateContext&) {
            return imtg::Error{imtg::ErrorCode::InvalidState, "stale callback"};
        },
        [](const imtg::telegram::UpdateContext&) { return imtg::Result<void>(); }}));
    CHECK(callback_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "fail",
        imtg::telegram::HandlerMode::Headless, {}, {},
        [](const imtg::telegram::UpdateContext&) {
            return imtg::Error{imtg::ErrorCode::RuntimeFailure, "handler failed"};
        }}));
    CHECK(callback_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "throw",
        imtg::telegram::HandlerMode::Headless, {}, {},
        [](const imtg::telegram::UpdateContext&) -> imtg::Result<void> {
            throw std::runtime_error("handler threw");
        }}));
    CHECK(callback_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "throw-unknown",
        imtg::telegram::HandlerMode::Headless, {}, {},
        [](const imtg::telegram::UpdateContext&) -> imtg::Result<void> {
            throw 7;
        }}));
    CHECK(callback_router.receive(callback_update(3, 7, -100, 42, "save"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(callback_router.receive(callback_update(4, 7, -101, 42, "save"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(wait_bounded(condition, lock, [&outcomes] {
            return std::find(outcomes.begin(), outcomes.end(),
                             imtg::telegram::UpdateOutcome::Unauthorized) != outcomes.end();
        }));
    }
    CHECK(callback_router.receive(callback_update(5, 7, -100, 42, "stale"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(callback_router.receive(callback_update(6, 7, -100, 42, "fail"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(callback_router.receive(callback_update(8, 7, -100, 42, "throw"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(callback_router.receive(callback_update(9, 7, -100, 42, "throw-unknown"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(wait_bounded(condition, lock, [&outcomes] {
            const auto stale = std::find(outcomes.begin(), outcomes.end(),
                                         imtg::telegram::UpdateOutcome::Stale);
            const auto failed = std::count(outcomes.begin(), outcomes.end(),
                                           imtg::telegram::UpdateOutcome::HandlerFailed);
            return stale != outcomes.end() && failed >= 3;
        }));
    }
    CHECK(callback_router.receive(callback_update(6, 7, -100, 42, "fail"))
          == imtg::telegram::UpdateOutcome::Duplicate);

    std::size_t wildcard_hits = 0;
    std::size_t exact_hits = 0;
    imtg::telegram::UpdateRouter ordering_router(runtime);
    CHECK(ordering_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&mutex, &condition, &wildcard_hits](const imtg::telegram::UpdateContext&) {
            std::lock_guard<std::mutex> lock(mutex);
            ++wildcard_hits;
            condition.notify_all();
            return imtg::Result<void>();
        }}));
    CHECK(ordering_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::CallbackQuery, {}, "exact",
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&mutex, &condition, &exact_hits](const imtg::telegram::UpdateContext&) {
            std::lock_guard<std::mutex> lock(mutex);
            ++exact_hits;
            condition.notify_all();
            return imtg::Result<void>();
        }}));
    CHECK(ordering_router.receive(callback_update(15, 7, -100, 42, "exact"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(mutex);
        CHECK(wait_bounded(condition, lock, [&exact_hits] { return exact_hits == 1; }));
    }
    CHECK(wildcard_hits == 0);
    CHECK(ordering_router.receive(callback_update(16, 7, 0, 0, "exact"))
          == imtg::telegram::UpdateOutcome::Invalid);

    callback_router.close();
    CHECK(callback_router.receive(callback_update(7, 7, -100, 42, "save"))
          == imtg::telegram::UpdateOutcome::Closed);

    std::mutex queue_mutex;
    std::condition_variable queue_condition;
    bool release = false;
    std::size_t queue_handled = 0;
    auto queue_runtime_config = test_runtime_config(1);
    queue_runtime_config.max_active_mailboxes = 1;
    imtg::ReferenceRuntime queue_runtime(
        actions, {}, {}, {}, queue_runtime_config);
    imtg::telegram::RouterConfig queue_router_config;
    queue_router_config.max_seen_updates = 1;
    imtg::telegram::UpdateRouter queue_router(queue_runtime, queue_router_config);
    CHECK(queue_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&queue_mutex, &queue_condition, &release, &queue_handled](
            const imtg::telegram::UpdateContext&) {
            std::unique_lock<std::mutex> lock(queue_mutex);
            ++queue_handled;
            queue_condition.notify_all();
            queue_condition.wait(lock, [&release] { return release; });
            return imtg::Result<void>();
        }}));
    CHECK(queue_router.receive(text_update(10, 9, -101, 42, "one"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        CHECK(wait_bounded(queue_condition, lock, [&queue_handled] { return queue_handled == 1; }));
    }
    CHECK(queue_router.receive(text_update(11, 9, -102, 42, "other-key"))
          == imtg::telegram::UpdateOutcome::QueueFull);
    CHECK(queue_router.receive(text_update(10, 9, -101, 42, "one"))
          == imtg::telegram::UpdateOutcome::Duplicate);
    CHECK(queue_router.receive(text_update(11, 9, -101, 42, "two"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(queue_router.receive(text_update(12, 9, -101, 42, "three"))
          == imtg::telegram::UpdateOutcome::QueueFull);
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        release = true;
    }
    queue_condition.notify_all();
    CHECK(queue_runtime.shutdown());
    CHECK(queue_handled == 2);

    std::mutex concurrent_mutex;
    std::condition_variable concurrent_condition;
    bool concurrent_started = false;
    bool concurrent_release = false;
    std::atomic<std::size_t> concurrent_accepted{0};
    std::atomic<std::size_t> concurrent_duplicates{0};
    imtg::ReferenceRuntime concurrent_runtime(
        actions, {}, {}, {}, test_runtime_config(4));
    imtg::telegram::UpdateRouter concurrent_router(concurrent_runtime);
    CHECK(concurrent_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&concurrent_mutex, &concurrent_condition, &concurrent_started,
         &concurrent_release](const imtg::telegram::UpdateContext&) {
            std::unique_lock<std::mutex> lock(concurrent_mutex);
            concurrent_started = true;
            concurrent_condition.notify_all();
            concurrent_condition.wait(lock, [&concurrent_release] {
                return concurrent_release;
            });
            return imtg::Result<void>();
        }}));
    std::vector<std::thread> duplicate_submitters;
    for (std::size_t index = 0; index < 8; ++index) {
        duplicate_submitters.emplace_back([&concurrent_router, &concurrent_accepted,
                                            &concurrent_duplicates] {
            const auto outcome = concurrent_router.receive(
                text_update(40, 21, -301, 42, "same"));
            if (outcome == imtg::telegram::UpdateOutcome::Accepted) {
                ++concurrent_accepted;
            } else if (outcome == imtg::telegram::UpdateOutcome::Duplicate) {
                ++concurrent_duplicates;
            }
        });
    }
    for (auto& submitter : duplicate_submitters) submitter.join();
    CHECK(concurrent_accepted.load() == 1);
    CHECK(concurrent_duplicates.load() == 7);
    {
        std::unique_lock<std::mutex> lock(concurrent_mutex);
        CHECK(wait_bounded(concurrent_condition, lock,
                           [&concurrent_started] { return concurrent_started; }));
        concurrent_release = true;
    }
    concurrent_condition.notify_all();
    CHECK(concurrent_runtime.shutdown());

    std::size_t identity_handled = 0;
    imtg::ReferenceRuntime identity_runtime(
        actions, {}, {}, {}, test_runtime_config(4));
    imtg::telegram::UpdateRouter identity_router(identity_runtime);
    CHECK(identity_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&identity_handled](const imtg::telegram::UpdateContext&) {
            ++identity_handled;
            return imtg::Result<void>();
        }}));
    CHECK(identity_router.receive(text_update(50, 30, -302, 42, "bot-a"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(identity_router.receive(text_update(50, 31, -302, 42, "bot-b"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(identity_router.receive(text_update(50, 30, -302, 42, "bot-a"))
          == imtg::telegram::UpdateOutcome::Duplicate);
    CHECK(identity_runtime.shutdown());
    CHECK(identity_handled == 2);

    std::size_t rollback_handled = 0;
    auto rollback_config = test_runtime_config(4);
    rollback_config.fail_queue_insertion_on_submit = 0;
    imtg::ReferenceRuntime rollback_runtime(
        actions, {}, {}, {}, rollback_config);
    imtg::telegram::UpdateRouter rollback_router(rollback_runtime);
    CHECK(rollback_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&rollback_handled](const imtg::telegram::UpdateContext&) {
            ++rollback_handled;
            return imtg::Result<void>();
        }}));
    CHECK(rollback_router.receive(text_update(60, 32, -303, 42, "retry"))
          == imtg::telegram::UpdateOutcome::QueueFull);
    CHECK(rollback_router.receive(text_update(60, 32, -303, 42, "retry"))
          == imtg::telegram::UpdateOutcome::Accepted);
    CHECK(rollback_runtime.shutdown());
    CHECK(rollback_handled == 1);

    std::mutex independent_mutex;
    std::condition_variable independent_condition;
    bool first_started = false;
    bool release_first = false;
    bool second_handled = false;
    auto independent_config = test_runtime_config(2);
    independent_config.worker_count = 2;
    imtg::ReferenceRuntime independent_runtime(
        actions, {}, {}, {}, independent_config);
    imtg::telegram::UpdateRouter independent_router(independent_runtime);
    CHECK(independent_router.add_route(imtg::telegram::Route{
        imtg::telegram::UpdateKind::TextMessage, {}, {},
        imtg::telegram::HandlerMode::Headless, {}, {},
        [&independent_mutex, &independent_condition, &first_started,
         &release_first, &second_handled](const imtg::telegram::UpdateContext& context) {
            const auto chat_id = std::get<imtg::telegram::TextMessage>(context.update.payload)
                                     .chat_id.value;
            if (chat_id == -201) {
                std::unique_lock<std::mutex> lock(independent_mutex);
                first_started = true;
                independent_condition.notify_all();
                independent_condition.wait(lock, [&release_first] { return release_first; });
            } else {
                std::lock_guard<std::mutex> lock(independent_mutex);
                second_handled = true;
                independent_condition.notify_all();
            }
            return imtg::Result<void>();
        }}));
    CHECK(independent_router.receive(text_update(30, 11, -201, 42, "first"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(independent_mutex);
        CHECK(wait_bounded(independent_condition, lock,
                           [&first_started] { return first_started; }));
    }
    CHECK(independent_router.receive(text_update(31, 11, -202, 42, "second"))
          == imtg::telegram::UpdateOutcome::Accepted);
    {
        std::unique_lock<std::mutex> lock(independent_mutex);
        CHECK(wait_bounded(independent_condition, lock,
                           [&second_handled] { return second_handled; }));
        release_first = true;
    }
    independent_condition.notify_all();
    CHECK(independent_runtime.shutdown());

    // Accepted callbacks retain completion state independently of the router,
    // so the router may be destroyed before the runtime drains its work.
    std::mutex lifetime_mutex;
    std::condition_variable lifetime_condition;
    bool lifetime_started = false;
    bool lifetime_release = false;
    std::size_t lifetime_handled = 0;
    imtg::ReferenceRuntime lifetime_runtime(
        actions, {}, {}, {}, test_runtime_config(2));
    {
        imtg::telegram::UpdateRouter lifetime_router(lifetime_runtime);
        CHECK(lifetime_router.add_route(imtg::telegram::Route{
            imtg::telegram::UpdateKind::TextMessage, {}, {},
            imtg::telegram::HandlerMode::Headless, {}, {},
            [&lifetime_mutex, &lifetime_condition, &lifetime_started,
             &lifetime_release, &lifetime_handled](const imtg::telegram::UpdateContext&) {
                std::unique_lock<std::mutex> lock(lifetime_mutex);
                lifetime_started = true;
                ++lifetime_handled;
                lifetime_condition.notify_all();
                lifetime_condition.wait(lock, [&lifetime_release] {
                    return lifetime_release;
                });
                return imtg::Result<void>();
            }}));
        CHECK(lifetime_router.receive(text_update(70, 41, -401, 42, "lifetime"))
              == imtg::telegram::UpdateOutcome::Accepted);
        std::unique_lock<std::mutex> lock(lifetime_mutex);
        CHECK(wait_bounded(lifetime_condition, lock,
                           [&lifetime_started] { return lifetime_started; }));
    }
    {
        std::lock_guard<std::mutex> lock(lifetime_mutex);
        lifetime_release = true;
    }
    lifetime_condition.notify_all();
    CHECK(lifetime_runtime.shutdown());
    CHECK(lifetime_handled == 1);

    CHECK(runtime.shutdown());
    return 0;
}
