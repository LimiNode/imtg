#include "imtg/ui/ui.hpp"

#include <cassert>

namespace {

imtg::UiTree render_tree()
{
    imtg::UiBuilder builder;
    {
        auto root_result = builder.begin("screen");
        assert(root_result);
        auto root = std::move(root_result).value();
        assert(builder.heading("title", "Settings"));
        auto section_result = builder.section("account", "Account");
        assert(section_result);
        auto section = std::move(section_result).value();
        assert(builder.text("name", "Ada"));
        auto row_result = builder.row("actions");
        assert(row_result);
        auto row = std::move(row_result).value();
        assert(builder.button("save", "Save", "settings.save.v1", "{}"));
        assert(builder.button("cancel", "Cancel", "settings.cancel.v1", "{}"));
    }
    const auto tree_result = builder.finish();
    assert(tree_result);
    return tree_result.value();
}

} // namespace

int main()
{
    const auto first = render_tree();
    const auto second = render_tree();
    assert(first == second);
    assert(first.nodes().size() == 7);
    assert(first.nodes()[0].path == "screen");
    assert(first.nodes()[1].path == "screen/title");
    assert(first.nodes()[2].path == "screen/account");
    assert(first.nodes()[4].path == "screen/account/actions");
    assert(first.nodes()[5].button->action_id == "settings.save.v1");

    imtg::UiBuilder duplicate_builder;
    {
        auto root_result = duplicate_builder.begin("screen");
        assert(root_result);
        auto root = std::move(root_result).value();
        assert(duplicate_builder.text("same", "first"));
        assert(!duplicate_builder.text("same", "second"));
    }
    assert(duplicate_builder.finish());

    imtg::UiBuilder invalid_builder;
    assert(!invalid_builder.text("outside", "invalid"));
    {
        auto invalid_root = invalid_builder.begin("screen");
        assert(invalid_root);
        auto root = std::move(invalid_root).value();
        assert(!invalid_builder.finish());
    }
    assert(invalid_builder.finish());
    assert(!invalid_builder.text("after", "invalid"));

    return 0;
}
