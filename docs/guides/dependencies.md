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
Do not add speculative submodules. For M2d, investigate sibling `external/mdbx-containers` and
`external/libmdbx` targets with an offline consumer test before adopting that layout.
