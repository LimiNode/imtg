#include "imtg/actions/actions.hpp"
#include "imtg/ui/ui.hpp"
#include "test_support.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <stdexcept>

namespace {

struct DeleteItem {
    std::uint64_t item_id{};
};

imtg::PayloadCodec<DeleteItem> delete_codec(std::uint32_t version)
{
    return imtg::PayloadCodec<DeleteItem>{
        version,
        [](const DeleteItem& item) -> imtg::Result<std::string> {
            return std::to_string(item.item_id);
        },
        [](std::string_view encoded) -> imtg::Result<DeleteItem> {
            try {
                return DeleteItem{static_cast<std::uint64_t>(std::stoull(std::string(encoded)))};
            } catch (...) {
                return imtg::Error{
                    imtg::ErrorCode::MalformedAction, "delete payload is malformed"};
            }
        }};
}

} // namespace

int main()
{
    const auto initial = imtg::ClockTime{std::chrono::seconds{100}};
    imtg::ManualClock clock(initial);
    imtg::PlainTokenCodec token_codec;
    imtg::ActionRegistry registry(token_codec, clock);
    std::mutex mutex;
    std::vector<std::uint64_t> dispatched;

    for (const auto& invalid_route : {"delete", "delete.v", "delete.v0", "delete.v01",
                                      "delete.vx", ".v1", "delete.v4294967296"}) {
        const auto rejected = registry.register_action<DeleteItem>(
            imtg::ActionId{invalid_route}, delete_codec(1),
            [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
        CHECK(!rejected);
    }

    const auto handle_result = registry.register_action<DeleteItem>(
        imtg::ActionId{"delete.v1"},
        delete_codec(1),
        [&mutex, &dispatched](imtg::ActionContext& context, const DeleteItem& item) {
            std::lock_guard<std::mutex> lock(mutex);
            dispatched.push_back(item.item_id);
            return context.dispatch(imtg::ActionCommand{"DeleteRequested", std::to_string(item.item_id)});
        });
    CHECK(handle_result);
    const auto handle = handle_result.value();
    CHECK(handle.is_valid());

    const auto token_result = registry.make_token(handle, DeleteItem{42});
    CHECK(token_result);
    CHECK(registry.invoke(token_result.value(), [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));
    CHECK(dispatched.size() == 1 && dispatched.front() == 42);

    imtg::UiBuilder builder;
    {
        auto root_result = builder.begin("screen");
        CHECK(root_result);
        auto root = std::move(root_result).value();
        CHECK(builder.button("delete", "Delete", handle, DeleteItem{99}));
    }
    const auto tree = builder.finish();
    CHECK(tree);
    CHECK(tree.value().nodes().at(1).button->action_id == "delete.v1");
    CHECK(tree.value().nodes().at(1).button->payload == "99");
    CHECK(tree.value().nodes().at(1).button->payload_version == 1);

    const auto expired_token = registry.make_token(
        handle, DeleteItem{7}, initial + std::chrono::seconds{1});
    CHECK(expired_token);
    clock.advance(std::chrono::seconds{2});
    CHECK(!registry.invoke(expired_token.value(), [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));

    CHECK(registry.unregister_action(imtg::ActionId{"delete.v1"}));
    const auto replacement_same_registry = registry.register_action<DeleteItem>(
        imtg::ActionId{"delete.v1"}, delete_codec(1),
        [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
    CHECK(replacement_same_registry);
    CHECK(!registry.make_token(handle, DeleteItem{8}));
    CHECK(registry.make_token(replacement_same_registry.value(), DeleteItem{8}));

    imtg::ActionRegistry restarted_registry(token_codec, clock);
    const auto restarted_handle = restarted_registry.register_action<DeleteItem>(
        imtg::ActionId{"delete.v1"}, delete_codec(1),
        [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
    CHECK(restarted_handle);
    CHECK(!restarted_registry.make_token(handle, DeleteItem{1}));
    CHECK(!registry.make_token(restarted_handle.value(), DeleteItem{2}));
    CHECK(restarted_registry.invoke(token_result.value(), [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));

    CHECK(!restarted_registry.invoke("malformed", [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));
    CHECK(!restarted_registry.invoke("v1|7:missing|1|1:7|-1", [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));

    imtg::ActionRegistry incompatible_registry(token_codec, clock);
    const auto incompatible = incompatible_registry.register_action<DeleteItem>(
        imtg::ActionId{"delete.v1"}, delete_codec(2),
        [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
    CHECK(incompatible);
    CHECK(!incompatible_registry.invoke(token_result.value(), [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));


    CHECK(restarted_registry.unregister_action(imtg::ActionId{"delete.v1"}));
    CHECK(!restarted_registry.invoke(token_result.value(), [](const imtg::ActionCommand&) {
        return imtg::Result<void>();
    }));

    const auto replacement = restarted_registry.register_action<DeleteItem>(
        imtg::ActionId{"delete.v1"}, delete_codec(1),
        [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
    CHECK(replacement);
    CHECK(!restarted_registry.make_token(handle, DeleteItem{3}));
    imtg::UiBuilder stale_builder(&restarted_registry);
    {
        auto root_result = stale_builder.begin("screen");
        CHECK(root_result);
        auto root = std::move(root_result).value();
        CHECK(!stale_builder.button("stale", "Stale", handle, DeleteItem{3}));
        CHECK(stale_builder.button("replacement", "Replacement", replacement.value(), DeleteItem{3}));
    }

    imtg::ActionRegistry concurrent_registry(token_codec, clock);
    const auto concurrent_handle = concurrent_registry.register_action<DeleteItem>(
        imtg::ActionId{"concurrent.v1"}, delete_codec(1),
        [](imtg::ActionContext&, const DeleteItem&) { return imtg::Result<void>(); });
    CHECK(concurrent_handle);
    const auto concurrent_token = concurrent_registry.make_token(
        concurrent_handle.value(), DeleteItem{9});
    CHECK(concurrent_token);
    std::vector<std::thread> workers;
    for (int index = 0; index < 4; ++index) {
        workers.emplace_back([&concurrent_registry, &concurrent_token] {
            CHECK(concurrent_registry.invoke(concurrent_token.value(),
                [](const imtg::ActionCommand&) { return imtg::Result<void>(); }));
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}
