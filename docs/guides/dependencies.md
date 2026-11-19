# Dependencies

Core must remain backend- and transport-neutral. Integrations are adapters behind imtg contracts: MDBX through `mdbx-containers`, HTTP through `kurlyk`, and event delivery through `event-hub-cpp` where its semantics fit. Dependency additions require an ownership/license/API review, a documented failure model, and a conformance or fake-based test plan.

The reviewed capability boundaries and exact source revisions for M2c/M2d/M3
planning are recorded in the [dependency capability audit](../DEPENDENCY_CAPABILITY_AUDIT.md).

Git-based dependencies are pinned direct submodules under `external/<name>` by default. Record the
reviewed source URL in `.gitmodules`, pin the gitlink SHA, and initialize non-recursively. Do not use
`FetchContent`, configure-time Git downloads, or recursive submodule initialization as an
unreviewed fallback. Prefer one shared root-level dependency target when multiple sibling libraries
can reuse it. Advancing a gitlink is a dedicated dependency update; do not patch upstream sources
silently.

System and platform libraries for which `find_package()` is the established integration are valid
exceptions. Unavoidable nested dependencies require a documented reason and explicit approval.
Do not add speculative submodules. M2d-a pins sibling `external/mdbx-containers`
and `external/libmdbx` submodules and provides them to CMake as one optional
dependency boundary. `IMTG_BUILD_MDBX=OFF` remains the default and performs no
MDBX lookup or download; `IMTG_BUILD_MDBX=ON` requires both initialized
submodules and uses `MDBXC_DEPS_MODE=BUNDLED` without a FetchContent fallback.
The optional adapter is tested with a local persistence/reopen consumer; its
installed-package export remains a later M2d-b boundary.

The optional `IMTG_BUILD_TELEGRAM_TRANSPORT` slice uses nlohmann/json privately
at `b1bb9fce0c9ae9db5cecf8e3bd78519ac70683ee`, the JSON gitlink selected by
`kurlyk@b62cd53f672c47b74bb14d762ea9dc59a9df19ce`. It is a flat
`external/json` submodule, initialized explicitly and non-recursively. Its MIT
notice is installed with the optional adapter, while its headers and package
lookup do not enter imtg's installed public contract. Core-only and the base
Telegram router require neither this parser nor kurlyk.
