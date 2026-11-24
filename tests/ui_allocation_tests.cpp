#include <imtg/ui.hpp>
#include "test_support.hpp"
#include <cstdlib>
#include <new>
#include <string>

namespace {
thread_local int remaining = -1;
thread_local bool injected = false;
}
void* operator new(std::size_t size)
{
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
    // Iterator-proxy allocation inside noexcept STL moves is not recoverable.
    const bool eligible = size > sizeof(std::_Container_proxy);
#else
    const bool eligible = true;
#endif
    if (eligible && remaining >= 0 && remaining-- == 0) {
        remaining = -1;
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    const std::string mode(argv[1]);
    CHECK(mode == "root" || mode == "text" || mode == "scope");
    bool reached_success = false;
    for (int index = 0; index != 128; ++index) {
        imtg::UiBuilder builder;
        bool caught = false;
        {
            imtg::UiBuilder::Scope root;
            if (mode != "root") {
                auto opened = builder.begin("root");
                CHECK(opened);
                root = std::move(opened).value();
            }
            injected = false;
            remaining = index;
            try {
                if (mode == "root") {
                    auto opened = builder.begin("root");
                    remaining = -1;
                    CHECK(opened);
                } else if (mode == "text") {
                    const auto appended = builder.text("child", "value");
                    remaining = -1;
                    CHECK(appended);
                } else {
                    auto opened = builder.section("child", "value");
                    remaining = -1;
                    CHECK(opened);
                }
            } catch (const std::bad_alloc&) {
                caught = true;
            }
            remaining = -1;
            if (caught) {
                const auto rejected = builder.text("after-failure", "value");
                CHECK(!rejected && rejected.error().code == imtg::ErrorCode::InvalidState);
            }
        }
        const auto tree = builder.finish();
        if (caught) {
            CHECK(injected && !tree);
        } else {
            CHECK(!injected && tree);
            CHECK(tree.value().nodes().size() == (mode == "root" ? 1 : 2));
            if (mode != "root") CHECK(tree.value().nodes()[0].children.size() == 1);
            reached_success = true;
            break;
        }
    }
    CHECK(reached_success);
}
