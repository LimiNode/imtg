#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include "allocation_diagnostic.hpp"

static bool armed = false;
void* operator new(std::size_t size)
{
    if (armed) {
        armed = false;
        allocation_diagnostic::out("DIAGNOSTIC_INJECT\n", 18);
        allocation_diagnostic::stack();
        throw std::bad_alloc();
    }
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
int main()
{
    std::fprintf(stderr, "MSC=%d ITERATOR=%d STL=%d proxy=%zu default_noexcept=%d\n",
        _MSC_FULL_VER, _ITERATOR_DEBUG_LEVEL, _MSVC_STL_VERSION,
        sizeof(std::_Container_proxy), noexcept(std::string{}));
    allocation_diagnostic::install();
    try {
        armed = true;
        std::string empty;
        armed = false;
    } catch (const std::bad_alloc&) {
        allocation_diagnostic::out("DIAGNOSTIC_CAUGHT\n", 18);
        return 42;
    }
    return 0;
}
