# C++ coding style

**Status:** normative for future implementation. **Semantic owner:** maintainers.

- C++17 is the minimum standard; use four spaces and braces on the declaration line.
- Classes, structs, enums, and aliases use `CamelCase`; all methods/free functions, including UI, use `snake_case`.
- Private/protected fields use `m_`; booleans prefer `is_`, `has_`, `use_`, or `enable_`.
- Enum values use `CamelCase`; project macros use `IMTG_`; namespaces use lowercase `snake_case`.
- Keep lines approximately 100–120 columns. Do not use `using namespace` in headers or source files.
- Do not use lambda default captures `[&]` or `[=]`; enumerate captures explicitly.
- Prefer RAII, value semantics, const-correctness, explicit ownership, and bounded allocation. Document lifetime and thread safety.
- Use `.hpp` for public declarations, `.cpp` for non-template implementation, `.tpp` for template implementation, and `.ipp` for header-visible helpers. One meaningful primary type per `CamelCase.hpp`; utility and domain umbrella headers use `snake_case.hpp`.
- Co-locate public headers and non-template implementations under `src/imtg/<domain>/`. Consumers
  continue to include `<imtg/...>`; the physical source layout must not leak into public includes.
- Each complete domain owns a root-level umbrella at `src/imtg/<domain>.hpp`.
  Public headers use an `IMTG_..._HPP` include guard (or an equivalent
  project-approved mechanism) and include only what they require. The header
  that defines a public type owns its declaration; umbrella headers re-export
  owned headers but do not create duplicate declarations. Domain dependencies
  form a DAG: lower-level public entry points may be included by higher-level
  umbrellas, but leaves never include their own umbrella. Core contracts do
  not include adapter headers.
- Cross-domain includes use `<imtg/...>` entry points; intra-domain includes
  use local paths. `..` relative includes and dependencies on consumer include
  order are forbidden. A public standalone header compiles as the first include
  in a translation unit. Private leaves may rely on a documented umbrella
  context only when they are excluded from the installed public manifest.
- The complete inventory and staged migration rules are maintained in
  [header architecture](header-architecture.md); do not perform a mechanical
  repository-wide relocation without a reviewed implementation PR.
- Do not rename third-party APIs. Naming exceptions require an ADR.

Every new public type and nontrivial public operation uses Doxygen in English in the same PR
as its implementation. Document semantics rather than repeating signatures, including
ownership, lifetime, errors, side effects, and thread safety. Preferred tags include `\brief`,
`\param`, `\return`, `\throws`, `\pre`, `\post`, `\invariant`, `\complexity`, and
`\thread_safety`. The strict optional validation workflow is described in
[public API documentation](public-api-documentation.md).
