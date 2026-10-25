#include "imtg/runtime/runtime.hpp"

#include <atomic>
#include <stdexcept>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#define CHECK(condition) \
    do { \
        if (!(condition)) throw std::runtime_error("check failed: " #condition); \
    } while (false)
#define assert(condition) CHECK(condition)

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
    assert(action);
    const auto token_one = actions.make_token(action.value(), CommandPayload{1});
    const auto token_two = actions.make_token(action.value(), CommandPayload{2});
    const auto token_three = actions.make_token(action.value(), CommandPayload{3});
    assert(token_one && token_two && token_three);

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
            assert(command.type == "Increment");
            if (command.payload == "1") {
                std::unique_lock<std::mutex> lock(gate_mutex);
                first_started = true;
                gate_condition.notify_all();
                gate_condition.wait(lock, [&] { return release_first; });
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
    assert(runtime.submit(key(10), token_one.value()));
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_condition.wait(lock, [&] { return first_started; });
    }
    assert(runtime.submit(key(20), token_three.value()));
    {
        std::unique_lock<std::mutex> lock(state_mutex);
        processed_condition.wait(lock, [&] { return processed.size() >= 2; });
    }
    assert(runtime.submit(key(10), token_two.value()));
    assert(!runtime.submit(key(10), token_one.value()));
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_first = true;
    }
    gate_condition.notify_all();
    assert(runtime.shutdown());
    assert(runtime.shutdown());
    assert(processed.size() == 3);
    assert(runtime.reconciler().revisions(key(10)).desired == 1);

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
        callback_condition.wait(lock, [&] { return release_callback; });
    });
    assert(subscription_result);
    auto subscription = std::move(subscription_result).value();
    std::thread publisher([&] { dispatcher.publish(imtg::Notification{"tick", "1"}); });
    {
        std::unique_lock<std::mutex> lock(callback_mutex);
        callback_condition.wait(lock, [&] { return callback_started; });
    }
    subscription.reset();
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        release_callback = true;
    }
    callback_condition.notify_all();
    publisher.join();
    dispatcher.publish(imtg::Notification{"tick", "2"});
    assert(callback_count.load() == 1);
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
    assert(nested_subscription);
    auto nested_handle = std::move(nested_subscription).value();
    nested_dispatcher.publish(imtg::Notification{"outer", "1"});
    assert(!nested_close);
    assert(reported_errors.load() >= 1);
    assert(nested_dispatcher.close_and_wait());

    bool invalid_config_rejected = false;
    try {
        imtg::RuntimeConfig invalid;
        invalid.worker_count = 0;
        imtg::ReferenceRuntime invalid_runtime(actions, {}, {}, {}, invalid);
    } catch (const std::invalid_argument&) {
        invalid_config_rejected = true;
    }
    assert(invalid_config_rejected);

    return 0;
}
