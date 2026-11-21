#include <imtg/runtime.hpp>
#include "test_support.hpp"

#include <condition_variable>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>

namespace {

thread_local int g_fail_allocation_after = -1;
thread_local bool g_allocation_failed = false;

} // namespace

void* operator new(std::size_t size)
{
    if (g_fail_allocation_after >= 0 && g_fail_allocation_after-- == 0) {
        g_fail_allocation_after = -1;
        g_allocation_failed = true;
        throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

struct SubscribingCapture {
    SubscribingCapture(imtg::NotificationDispatcher& dispatcher, bool& completed)
        : dispatcher(dispatcher), completed(completed) {}
    ~SubscribingCapture()
    {
        auto result = dispatcher.subscribe([](const imtg::Notification&) {});
        completed = static_cast<bool>(result);
    }
    imtg::NotificationDispatcher& dispatcher;
    bool& completed;
};

void subscription_allocation_failure(int fail_after)
{
    imtg::NotificationDispatcher dispatcher;
    bool cleanup_completed = false;
    const auto prepare_callback = [&] {
        auto capture = std::make_shared<SubscribingCapture>(dispatcher, cleanup_completed);
        imtg::NotificationDispatcher::Callback callback = [capture](const imtg::Notification&) {};
        g_fail_allocation_after = fail_after;
        return callback;
    };
    bool threw = false;
    try {
        // A moved-from std::function may retain its target; keep no caller owner.
        (void)dispatcher.subscribe(prepare_callback());
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    g_fail_allocation_after = -1;
    CHECK(threw);
    CHECK(g_allocation_failed);
    CHECK(cleanup_completed);
    CHECK(dispatcher.close_and_wait());
}

void nested_subscription_cleanup(bool close)
{
    imtg::NotificationDispatcher dispatcher;
    auto inner_result = dispatcher.subscribe([](const imtg::Notification&) {});
    CHECK(inner_result);
    auto inner = std::make_shared<imtg::NotificationSubscription>(
        std::move(inner_result).value());
    std::weak_ptr<imtg::NotificationSubscription> observed = inner;
    auto outer_result = dispatcher.subscribe([inner](const imtg::Notification&) {});
    CHECK(outer_result);
    auto outer = std::move(outer_result).value();
    inner.reset();
    if (close) {
        CHECK(dispatcher.close_and_wait());
    } else {
        outer.reset();
    }
    CHECK(observed.expired());
    CHECK(dispatcher.close_and_wait());
}

struct CleanupGate {
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool release = false;
};

struct BlockingCapture {
    explicit BlockingCapture(CleanupGate& gate) : gate(gate) {}
    ~BlockingCapture()
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.started = true;
        gate.condition.notify_all();
        gate.condition.wait(lock, [this] { return gate.release; });
    }
    CleanupGate& gate;
};

void retirement_quiescence(bool close)
{
    imtg::NotificationDispatcher dispatcher;
    CleanupGate gate;
    auto capture = std::make_shared<BlockingCapture>(gate);
    auto result = dispatcher.subscribe([capture](const imtg::Notification&) {});
    CHECK(result);
    auto subscription = std::move(result).value();
    capture.reset();
    std::thread retiring([close, &dispatcher, &subscription] {
        if (close) {
            CHECK(dispatcher.close_and_wait());
        } else {
            subscription.reset();
        }
    });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        CHECK(wait_bounded(gate.condition, lock, [&gate] { return gate.started; }));
    }
    std::promise<void> waiting_started;
    auto waiting_ready = waiting_started.get_future();
    auto waiting = std::async(std::launch::async, [&dispatcher, &waiting_started] {
        waiting_started.set_value();
        return dispatcher.close_and_wait();
    });
    CHECK(waiting_ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.release = true;
    }
    gate.condition.notify_all();
    retiring.join();
    CHECK(waiting.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(waiting.get());
}

struct ClosingCapture {
    ClosingCapture(imtg::NotificationDispatcher& dispatcher, bool& rejected)
        : dispatcher(dispatcher), rejected(rejected) {}
    ~ClosingCapture()
    {
        const auto result = dispatcher.close_and_wait();
        rejected = !result && result.error().code == imtg::ErrorCode::InvalidState;
    }
    imtg::NotificationDispatcher& dispatcher;
    bool& rejected;
};

void retirement_self_wait(bool close)
{
    imtg::NotificationDispatcher dispatcher;
    bool rejected = false;
    auto capture = std::make_shared<ClosingCapture>(dispatcher, rejected);
    auto result = dispatcher.subscribe([capture](const imtg::Notification&) {});
    CHECK(result);
    auto subscription = std::move(result).value();
    capture.reset();
    if (close) {
        CHECK(dispatcher.close_and_wait());
    } else {
        subscription.reset();
    }
    CHECK(rejected);
    CHECK(dispatcher.close_and_wait());
}

imtg::PayloadCodec<int> codec()
{
    return {1,
        [](const int&) -> imtg::Result<std::string> { return std::string("0"); },
        [](std::string_view) -> imtg::Result<int> { return 0; }};
}

struct UnregisteringCapture {
    UnregisteringCapture(imtg::ActionRegistry& registry, bool& removed)
        : registry(registry), removed(removed) {}
    ~UnregisteringCapture()
    {
        removed = static_cast<bool>(registry.unregister_action(imtg::ActionId{"other.v1"}));
    }
    imtg::ActionRegistry& registry;
    bool& removed;
};

void action_capture_cleanup()
{
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec token_codec;
    imtg::ActionRegistry registry(token_codec, clock);
    bool removed = false;
    CHECK(registry.register_action<int>(imtg::ActionId{"other.v1"}, codec(),
        [](imtg::ActionContext&, const int&) { return imtg::Result<void>(); }));
    auto capture = std::make_shared<UnregisteringCapture>(registry, removed);
    const auto handle = registry.register_action<int>(imtg::ActionId{"primary.v1"}, codec(),
        [capture](imtg::ActionContext&, const int&) { return imtg::Result<void>(); });
    CHECK(handle);
    capture.reset();
    CHECK(registry.unregister_action(imtg::ActionId{"primary.v1"}));
    CHECK(removed);
    CHECK(!handle.value().validate());
    CHECK(!registry.unregister_action(imtg::ActionId{"other.v1"}));
}

void action_in_flight_cleanup()
{
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec token_codec;
    imtg::ActionRegistry registry(token_codec, clock);
    bool removed = false;
    CHECK(registry.register_action<int>(imtg::ActionId{"other.v1"}, codec(),
        [](imtg::ActionContext&, const int&) { return imtg::Result<void>(); }));
    CleanupGate gate;
    auto capture = std::make_shared<UnregisteringCapture>(registry, removed);
    const auto handle = registry.register_action<int>(imtg::ActionId{"primary.v1"}, codec(),
        [capture, &gate](imtg::ActionContext&, const int&) {
            std::unique_lock<std::mutex> lock(gate.mutex);
            gate.started = true;
            gate.condition.notify_all();
            gate.condition.wait(lock, [&gate] { return gate.release; });
            return imtg::Result<void>();
        });
    CHECK(handle);
    const auto token = registry.make_token(handle.value(), 0);
    CHECK(token);
    capture.reset();
    std::thread invocation([&registry, &token] {
        CHECK(registry.invoke(token.value(),
            [](const imtg::ActionCommand&) { return imtg::Result<void>(); }));
    });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        CHECK(wait_bounded(gate.condition, lock, [&gate] { return gate.started; }));
        CHECK(registry.unregister_action(imtg::ActionId{"primary.v1"}));
        CHECK(!removed);
        gate.release = true;
    }
    gate.condition.notify_all();
    invocation.join();
    CHECK(removed);
    CHECK(!handle.value().validate());
}

} // namespace

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string mode(argv[1]);
    if (mode == "reset") nested_subscription_cleanup(false);
    else if (mode == "close") nested_subscription_cleanup(true);
    else if (mode == "reset_quiescence") retirement_quiescence(false);
    else if (mode == "close_quiescence") retirement_quiescence(true);
    else if (mode == "reset_self_wait") retirement_self_wait(false);
    else if (mode == "close_self_wait") retirement_self_wait(true);
    else if (mode == "unregister") action_capture_cleanup();
    else if (mode == "action_in_flight") action_in_flight_cleanup();
    else if (mode == "subscribe_entry_failure") subscription_allocation_failure(0);
    else if (mode == "subscribe_vector_failure") subscription_allocation_failure(1);
    else CHECK(false);
}
