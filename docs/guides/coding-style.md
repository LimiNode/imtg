# C++ coding style

**Status:** normative for future implementation. **Semantic owner:** maintainers.

- C++17 is the minimum standard; use four spaces and braces on the declaration line.
- Classes, structs, enums, and aliases use `CamelCase`; all methods/free functions, including UI, use `snake_case`.
- Private/protected fields use `m_`; booleans prefer `is_`, `has_`, `use_`, or `enable_`.
- Enum values use `CamelCase`; project macros use `IMTG_`; namespaces use lowercase `snake_case`.
- Keep lines approximately 100–120 columns. Do not use `using namespace` in headers or source files.
- Do not use lambda default captures `[&]` or `[=]`; enumerate captures explicitly.
- Prefer RAII, value semantics, const-correctness, explicit ownership, and bounded allocation. Document lifetime and thread safety.
- Use `.hpp` for public declarations, `.cpp` for non-template implementation, `.tpp` for template implementation, and `.ipp` for header-visible helpers. One primary type per `CamelCase.hpp`; utility/umbrella headers use `snake_case.hpp`.
- Do not rename third-party APIs. Naming exceptions require an ADR.

Public APIs use Doxygen in English. Document semantics rather than repeating signatures, including ownership, lifetime, errors, side effects, and thread safety. Preferred tags include `\\brief`, `\\param`, `\\return`, `\\throws`, `\\pre`, `\\post`, `\\invariant`, `\\complexity`, and `\\thread_safety`.
