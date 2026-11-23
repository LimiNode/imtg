#ifndef IMTG_TEST_ALLOCATION_DIAGNOSTIC_HPP
#define IMTG_TEST_ALLOCATION_DIAGNOSTIC_HPP

// Diagnostic-only stack addresses use fixed buffers and OS I/O, never C++ allocation.
#if defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <io.h>
#include <cstdlib>
#include <exception>

namespace allocation_diagnostic {
inline bool enabled = false;
inline void out(const char* text, unsigned size) noexcept { (void)_write(2, text, size); }
inline void stack() noexcept
{
    void* frames[48];
    const auto count = CaptureStackBackTrace(0, 48, frames, nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < count; ++i) {
        auto offset = reinterpret_cast<std::uintptr_t>(frames[i]) - base;
        char line[19] = {'0', 'x'};
        for (int j = 17; j >= 2; --j) { line[j] = digits[offset & 15]; offset >>= 4; }
        line[18] = '\n';
        out(line, 19);
    }
}
inline void install() noexcept
{
    enabled = true;
    std::set_terminate([] {
        out("DIAGNOSTIC_TERMINATE\n", 21);
        stack();
        std::_Exit(77);
    });
}
} // namespace allocation_diagnostic
#endif
#endif
