# imtg

imtg is a C++17 framework design for Telegram bots whose views are described with an Immediate-Mode UI model. It combines deterministic UI descriptions with callback-based actions, typed events, controller-friendly state, and replaceable persistence.

## Project status

This repository has completed **M1 — Telegram-free deterministic foundation** and implements the
**M2a backend-neutral storage foundation and M2b typed session/FSM baseline**. The C++17 `imtg_core` target includes deterministic
UI/action primitives, a bounded reference runtime, snapshot/optimistic transactions, monotonic
record generations, schema gates, explicit commit outcomes, typed session records, deterministic FSM
transitions, processed-update deduplication, minimal atomic render intents, and reusable offline
conformance tests. Durable process-crash persistence, MDBX, claim workers, outbox delivery, Telegram
transport, and release artifacts remain planned. Code fragments remain conceptual unless identified as implemented in
[IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md).

## Design in one minute

An application redraws a screen from current state for each logical update. Stable component IDs make the resulting UI tree comparable across renders. Application composition registers a stable, versioned action route; a button binds its serializable payload to that route. A later Telegram update resolves the pre-registered handler, which runs only in a short-lived `ActionContext`. Required changes use transactional commands and durable render intents; typed events may notify other services. Rendering and delivery remain separate so Telegram failures can be retried through an outbox without replaying arbitrary C++ callbacks.

```cpp
// Conceptual API; M1c provides the safe ActionHandle binding and registry.
const auto save_action = app.actions().register_action<SaveRequested>(
    "settings.save.v1",
    [](imtg::ActionContext& ctx, const SaveRequested& command) {
        ctx.dispatch(command);
    });

void settings_screen(imtg::ViewContext& view, imtg::ActionHandle save_action) {
    view.ui().begin("settings");
    view.ui().text("Settings");
    view.ui().button("save", "Save", SaveRequested{}, save_action);
    view.ui().end();
}
```

All imtg functions and methods, including the public Immediate-Mode UI surface, use `snake_case` (`ui.button()`, `ui.input_text()`, `ctx.navigate()`). Types use `CamelCase`. Dear ImGui informs the programming model, not imtg's spelling convention.

## Architecture at a glance

```text
Telegram update -> stateless gateway -> conversation scheduler
                                             |
                                             v
                                  action router + command
                                             |
                                             v
                                 state + durable render intent
                                             |
                                             v
                                  renderer/reconciler -> outbox -> delivery
```

The core owns contracts and deterministic orchestration. `kurlyk` is a planned transport integration, and MDBX through `mdbx-containers` is the first planned storage backend; neither is a core dependency.

## Documentation map

- [Architecture](docs/ARCHITECTURE.md) — boundaries and data flow.
- [Invariants](docs/INVARIANTS.md) — rules that implementations must preserve.
- [Roadmap](docs/ROADMAP.md) — dependency-gated milestones and acceptance criteria.
- [Implementation status](docs/IMPLEMENTATION_STATUS.md) — what exists today.
- [Reference scenarios](docs/reference-scenarios.md) — end-to-end design examples.
- [Sessions and FSM](docs/contracts/SESSIONS_AND_FSM.md) — typed session and transactional transition contract.
- [Engineering governance](docs/DOCUMENTATION_GOVERNANCE.md) — how contracts stay canonical.
- [Coding style](docs/guides/coding-style.md) — C++17, naming, ownership, and Doxygen rules.
- [Public API documentation](docs/guides/public-api-documentation.md) — semantic coverage and strict validation.

Normative contracts live in [`docs/contracts/`](docs/contracts/). Significant decisions are recorded in [`docs/adr/`](docs/adr/).

## Building

Keep all generated output under the ignored root `build/` directory:

```text
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Public headers and their implementations are co-located under `src/imtg/<domain>/`, while consumer
include paths remain `<imtg/...>`. Git-based dependencies, when required, are pinned direct
submodules under `external/<name>` and initialized non-recursively by default.

## Next step

The next slice is M2c: render-intent processing, reconciliation, and durable outbox claims on top of the accepted M2b transaction boundary. See the roadmap for the full dependency chain.
