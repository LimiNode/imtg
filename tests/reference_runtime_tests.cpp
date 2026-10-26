#include "imtg/runtime/runtime.hpp"
#include "test_support.hpp"

#include <atomic>
#include <stdexcept>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct CommandPayload {
    int value{};
};

imtg::PayloadCodec<CommandPayload> command_codec()
{
    return imtg::PayloadCodec<CommandPayload>{
        1,
        [](const CommandPayload& payload) -> imtg::Result<std::string> {
            return std::to_string(payload.value);
        },
        [](std::string_view value) -> imtg::Result<CommandPayload> {
            try {
                return CommandPayload{std::stoi(std::string(value))};
            } catch (...) {
                return imtg::Error{imtg::ErrorCode::MalformedAction, "invalid command payload"};
            }
        }};
}

imtg::ConversationKey key(std::int64_t chat)
{
    return imtg::ConversationKey{imtg::BotId{1}, imtg::ChatId{chat}, imtg::Chat{}};
}

} // namespace

int main()
{
    imtg::ManualClock clock(imtg::ClockTime{std::chrono::seconds{1}});
    imtg::PlainTokenCodec codec;
    imtg::ActionRegistry actions(codec, clock);
    std::mutex state_mutex;
    std::condition_variable processed_condition;
    std::vector<int> processed;
    const auto action = actions.register_action<CommandPayload>(
        imtg::ActionId{"counter.v1"}, command_codec(),
        [&state_mutex, &processed, &processed_condition](imtg::ActionContext& context,
                                                         const CommandPayload& payload) {
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                processed.push_back(payload.value);
                processed_condition.notify_all();
            }
            return context.dispatch(imtg::ActionCommand{"Increment", std::to_string(payload.value)});
        });
    CHECK(action);
    const auto token_one = actions.make_token(action.value(), CommandPayload{1});
    const auto token_two = actions.make_token(action.value(), CommandPayload{2});
    const auto token_three = actions.make_token(action.value(), CommandPayload{3});
    CHECK(token_one && token_two && token_three);

    std::mutex error_mutex;
    std::vector<imtg::ErrorCode> errors;
    std::mutex gate_mutex;
    std::condition_variable gate_condition;
    bool first_started = false;
    bool release_first = false;
    imtg::ReferenceRuntime* runtime_ptr = nullptr;
    imtg::ReferenceRuntime runtime(
        actions,
        [](const imtg::ConversationKey&) -> imtg::Result<imtg::UiTree> {
            imtg::UiBuilder builder;
            auto root_result = builder.begin("screen");
            if (!root_result) {
                return root_result.error();
            }
            auto root = std::move(root_result).value();
            if (!builder.text("status", "ok")) {
                return imtg::Error{imtg::ErrorCode::InvalidState, "cannot render status"};
            }
            root = imtg::UiBuilder::Scope{};
            return builder.finish();
        },
        [&state_mutex, &processed, &runtime_ptr, &gate_mutex, &gate_condition,
         &first_started, &release_first](const imtg::ConversationKey&,
                                         const imtg::ActionCommand& command) {
            CHECK(command.type == "Increment");
            if (command.payload == "1") {
                std::unique_lock<std::mutex> lock(gate_mutex);
                first_started = true;
                gate_condition.notify_all();
                gate_condition.wait_for(lock, std::chrono::seconds(2), [&] { return release_first; });
            }
            if (runtime_ptr != nullptr && command.payload == "2") {
                runtime_ptr->request_shutdown();
            }
            return imtg::Result<void>();
        },
        [&error_mutex, &errors](const imtg::ConversationKey&, const imtg::Error& error) {
            std::lock_guard<std::mutex> lock(error_mutex);
            errors.push_back(error.code);
        },
        1);
    runtime_ptr = &runtime;
    CHECK(runtime.submit(key(10), token_one.value()));
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        CHECK(gate_condition.wait_for(lock, std::chrono::seconds(2), [&] { return first_started; }));
    }
    CHECK(runtime.submit(key(20), token_three.value()));
    {
        std::unique_lock<std::mutex> lock(state_mutex);
        CHECK(processed_condition.wait_for(lock, std::chrono::seconds(2), [&] { return processed.size() >= 2; }));
    }
    CHECK(runtime.submit(key(10), token_two.value()));
    CHECK(!runtime.submit(key(10), token_one.value()));
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_first = true;
    }
    gate_condition.notify_all();
    CHECK(runtime.shutdown());
    CHECK(runtime.shutdown());
    CHECK(processed.size() == 3);
    CHECK(runtime.reconciler().revisions(key(10)).desired == 1);

    imtg::NotificationDispatcher dispatcher;
    std::mutex callback_mutex;
    std::condition_variable callback_condition;
    bool callback_started = false;
    bool release_callback = false;
    std::atomic<int> callback_count{0};
    auto subscription_result = dispatcher.subscribe([&](const imtg::Notification&) {
        callback_count.fetch_add(1);
        std::unique_lock<std::mutex> lock(callback_mutex);
        callback_started = true;
        callback_condition.notify_all();
        callback_condition.wait_for(lock, std::chrono::seconds(2), [&] { return release_callback; });
    });
    CHECK(subscription_result);
    auto subscription = std::move(subscription_result).value();
    std::thread publisher([&] { dispatcher.publish(imtg::Notification{"tick", "1"}); });
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        CHECK(callback_condition.wait_for(lock, std::chrono::seconds(2), [&] { return callback_started; }));
    }
    subscription.reset();
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        release_callback = true;
    }
    callback_condition.notify_all();
    publisher.join();
    dispatcher.publish(imtg::Notification{"tick", "2"});
    CHECK(callback_count.load() == 1);
    CHECK(dispatcher.close_and_wait());

    std::atomic<int> reported_errors{0};
    imtg::NotificationDispatcher nested_dispatcher(
        [&](std::exception_ptr) { reported_errors.fetch_add(1); });
    imtg::Result<void> nested_close;
    auto nested_subscription = nested_dispatcher.subscribe(
        [&](const imtg::Notification&) {
            nested_dispatcher.publish(imtg::Notification{"nested", "1"});
            nested_close = nested_dispatcher.close_and_wait();
            throw std::runtime_error("callback failure");
        });
    CHECK(nested_subscription);
    auto nested_handle = std::move(nested_subscription).value();
    nested_dispatcher.publish(imtg::Notification{"outer", "1"});
    CHECK(!nested_close);
    CHECK(reported_errors.load() >= 1);
    CHECK(nested_dispatcher.close_and_wait());

    // A failing callback must not let an error sink self-wait on a publication
    // that still has another selected callback to run.
    imtg::NotificationDispatcher* multi_ptr = nullptr;
    imtg::Result<void> sink_close;
    std::atomic<int> second_callback_count{0};
    imtg::NotificationDispatcher multi_dispatcher([&](std::exception_ptr) {
        sink_close = multi_ptr->close_and_wait();
    });
    multi_ptr = &multi_dispatcher;
    auto first = multi_dispatcher.subscribe([](const imtg::Notification&) {
        throw std::runtime_error("first callback failure");
    });
    auto second = multi_dispatcher.subscribe([&](const imtg::Notification&) {
        second_callback_count.fetch_add(1);
    });
    CHECK(first && second);
    auto publish_future = std::async(std::launch::async, [&] {
        multi_dispatcher.publish(imtg::Notification{"multi", "1"});
    });
    CHECK(publish_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    publish_future.get();
    CHECK(!sink_close);
    CHECK(sink_close.error().code == imtg::ErrorCode::InvalidState);
    CHECK(second_callback_count.load() == 1);
    CHECK(multi_dispatcher.close_and_wait());

    // Recursive error reporting is bounded by the publication-depth guard.
    std::atomic<int> recursive_callbacks{0};
    imtg::NotificationDispatcher* recursive_ptr = nullptr;
    imtg::NotificationDispatcher recursive_dispatcher([&](std::exception_ptr) {
        if (recursive_callbacks.load() < 200) {
            recursive_ptr->publish(imtg::Notification{"recursive", "1"});
        }
    });
    recursive_ptr = &recursive_dispatcher;
    auto recursive_subscription = recursive_dispatcher.subscribe(
        [&](const imtg::Notification&) {
            recursive_callbacks.fetch_add(1);
            throw std::runtime_error("recursive callback failure");
        });
    CHECK(recursive_subscription);
    recursive_dispatcher.publish(imtg::Notification{"recursive", "0"});
    CHECK(recursive_callbacks.load() > 0);
    CHECK(recursive_callbacks.load() <= 64);
    CHECK(recursive_dispatcher.close_and_wait());

    // Deterministic startup and requeue failure policies are observable.
    {
        imtg::RuntimeConfig startup_config;
        startup_config.worker_count = 3;
        startup_config.fail_worker_start_after = 1;
        bool startup_failed = false;
        try {
            imtg::ReferenceRuntime startup_failure(actions, {}, {}, {}, startup_config);
        } catch (const std::runtime_error&) {
            startup_failed = true;
        }
        CHECK(startup_failed);
    }
    {
        imtg::RuntimeConfig failure_config;
        failure_config.worker_count = 1;
        failure_config.fail_ready_requeue = true;
        std::mutex failure_mutex;
        std::condition_variable failure_condition;
        bool started = false;
        bool release = false;
        imtg::ReferenceRuntime failure_runtime(
            actions, {},
            [&](const imtg::ConversationKey&, const imtg::ActionCommand&) {
                std::unique_lock<std::mutex> lock(failure_mutex);
                started = true;
                failure_condition.notify_all();
                failure_condition.wait_for(lock, std::chrono::seconds(2), [&] { return release; });
                return imtg::Result<void>();
            },
            {}, failure_config);
        CHECK(failure_runtime.submit(key(99), token_one.value()));
        {
            std::unique_lock<std::mutex> lock(failure_mutex);
            CHECK(failure_condition.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
        }
        CHECK(failure_runtime.submit(key(99), token_two.value()));
        {
            std::lock_guard<std::mutex> lock(failure_mutex);
            release = true;
        }
        failure_condition.notify_all();
        const auto failure_result = failure_runtime.shutdown();
        CHECK(!failure_result);
        CHECK(failure_result.error().code == imtg::ErrorCode::RuntimeFailure);
        CHECK(failure_runtime.discarded_task_count() == 1);
        CHECK(!failure_runtime.failure_status());
        CHECK(!failure_runtime.submit(key(99), token_three.value()));
    }

    for (const auto worker_count : {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
        imtg::RuntimeConfig count_config;
        count_config.worker_count = worker_count;
        imtg::ReferenceRuntime count_runtime(actions, {}, {}, {}, count_config);
        CHECK(count_runtime.submit(key(static_cast<std::int64_t>(worker_count + 200)),
                                   token_three.value()));
        CHECK(count_runtime.shutdown());
    }

    {
        imtg::RuntimeConfig limit_config;
        limit_config.worker_count = 1;
        limit_config.max_active_mailboxes = 1;
        limit_config.max_queued_tasks_total = 1;
        std::mutex limit_mutex;
        std::condition_variable limit_condition;
        bool limit_started = false;
        bool limit_release = false;
        imtg::ReferenceRuntime limit_runtime(
            actions, {},
            [&](const imtg::ConversationKey&, const imtg::ActionCommand&) {
                std::unique_lock<std::mutex> lock(limit_mutex);
                limit_started = true;
                limit_condition.notify_all();
                limit_condition.wait_for(lock, std::chrono::seconds(2), [&] { return limit_release; });
                return imtg::Result<void>();
            },
            {}, limit_config);
        CHECK(limit_runtime.submit(key(301), token_one.value()));
        {
            std::unique_lock<std::mutex> lock(limit_mutex);
            CHECK(limit_condition.wait_for(lock, std::chrono::seconds(2), [&] { return limit_started; }));
        }
        CHECK(!limit_runtime.submit(key(302), token_two.value()));
        {
            std::lock_guard<std::mutex> lock(limit_mutex);
            limit_release = true;
        }
        limit_condition.notify_all();
        CHECK(limit_runtime.shutdown());
    }

    {
        imtg::ReferenceRuntime concurrent_shutdown(actions, {}, {}, {});
        imtg::Result<void> shutdown_one;
        imtg::Result<void> shutdown_two;
        std::thread first_shutdown([&] { shutdown_one = concurrent_shutdown.shutdown(); });
        std::thread second_shutdown([&] { shutdown_two = concurrent_shutdown.shutdown(); });
        first_shutdown.join();
        second_shutdown.join();
        CHECK(shutdown_one);
        CHECK(shutdown_two);
    }

    bool invalid_config_rejected = false;
    try {
        imtg::RuntimeConfig invalid;
        invalid.worker_count = 0;
        imtg::ReferenceRuntime invalid_runtime(actions, {}, {}, {}, invalid);
    } catch (const std::invalid_argument&) {
        invalid_config_rejected = true;
    }
    CHECK(invalid_config_rejected);

    return 0;
}
