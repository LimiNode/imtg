# imtg

imtg is a C++17 framework design for Telegram bots whose views are described with an Immediate-Mode UI model. It combines deterministic UI descriptions with callback-based actions, typed events, controller-friendly state, and replaceable persistence.

## Project status

This repository is at **M1c — Action Registry & Callback Semantics**. The C++17 `imtg_core` target, typed identities, deterministic clocks, immutable UI tree, RAII grouping scopes, stable paths, typed versioned action registry, injectable token codec boundary, and offline tests are implemented. Conversation scheduler, storage adapters, Telegram authentication/transport, and release artifacts are not implemented yet. Code fragments in the documentation remain conceptual unless identified as implemented in [IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md).

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
- [Engineering governance](docs/DOCUMENTATION_GOVERNANCE.md) — how contracts stay canonical.
- [Coding style](docs/guides/coding-style.md) — C++17, naming, ownership, and Doxygen rules.

Normative contracts live in [`docs/contracts/`](docs/contracts/). Significant decisions are recorded in [`docs/adr/`](docs/adr/).

## Next step

The next stacked PR is M1d: a deterministic per-conversation scheduler and reference runtime. See the roadmap for the full dependency chain.
