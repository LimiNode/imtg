#include "imtg/ui/ui.hpp"

#include <stdexcept>

#define CHECK(condition) \
    do { \
        if (!(condition)) throw std::runtime_error("check failed: " #condition); \
    } while (false)

namespace {

imtg::UiTree render_tree()
{
    imtg::UiBuilder builder;
    {
        auto root_result = builder.begin("screen");
        CHECK(root_result);
        auto root = std::move(root_result).value();
        CHECK(builder.heading("title", "Settings"));
        auto section_result = builder.section("account", "Account");
        CHECK(section_result);
        auto section = std::move(section_result).value();
        CHECK(builder.text("name", "Ada"));
        auto row_result = builder.row("actions");
        CHECK(row_result);
        auto row = std::move(row_result).value();
        CHECK(builder.button("save", "Save", "settings.save.v1", "{}"));
        CHECK(builder.button("cancel", "Cancel", "settings.cancel.v1", "{}"));
    }
    const auto tree_result = builder.finish();
    CHECK(tree_result);
    return tree_result.value();
}

} // namespace

int main()
{
    const auto first = render_tree();
    const auto second = render_tree();
    CHECK(first == second);
    CHECK(first.nodes().size() == 7);
    CHECK(first.nodes()[0].path == "screen");
    CHECK(first.nodes()[1].path == "screen/title");
    CHECK(first.nodes()[2].path == "screen/account");
    CHECK(first.nodes()[4].path == "screen/account/actions");
    CHECK(first.nodes()[5].button->action_id == "settings.save.v1");

    imtg::UiBuilder duplicate_builder;
    {
        auto root_result = duplicate_builder.begin("screen");
        CHECK(root_result);
        auto root = std::move(root_result).value();
        CHECK(duplicate_builder.text("same", "first"));
        CHECK(!duplicate_builder.text("same", "second"));
    }
    CHECK(duplicate_builder.finish());

    imtg::UiBuilder invalid_builder;
    CHECK(!invalid_builder.text("outside", "invalid"));
    {
        auto invalid_root = invalid_builder.begin("screen");
        CHECK(invalid_root);
        auto root = std::move(invalid_root).value();
        CHECK(!invalid_builder.finish());
    }
    CHECK(invalid_builder.finish());
    CHECK(!invalid_builder.text("after", "invalid"));

    imtg::UiBuilder collision_builder;
    {
        auto root_result = collision_builder.begin("root");
        CHECK(root_result);
        auto root = std::move(root_result).value();
        CHECK(!collision_builder.text("a/b", "invalid id"));
        auto section_result = collision_builder.section("a");
        CHECK(section_result);
        auto section = std::move(section_result).value();
        CHECK(collision_builder.text("b", "nested"));
    }
    CHECK(collision_builder.finish());

    imtg::UiBuilder order_builder;
    auto order_root_result = order_builder.begin("root");
    CHECK(order_root_result);
    auto order_root = std::move(order_root_result).value();
    auto order_section_result = order_builder.section("section");
    CHECK(order_section_result);
    auto order_section = std::move(order_section_result).value();
    order_root = std::move(order_section);
    CHECK(!order_builder.finish());

    return 0;
}
