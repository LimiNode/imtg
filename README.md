# imtg

imtg is a C++17 framework design for Telegram bots whose views are described with an Immediate-Mode UI model. It combines deterministic UI descriptions with callback-based actions, typed events, controller-friendly state, and replaceable persistence.

## Project status

This repository is at **D0 — Architecture & Engineering Foundation**. It currently contains design documentation only. There is no production runtime, Telegram transport, UI implementation, storage adapter, build target, or release artifact yet. Code fragments in the documentation are conceptual contracts, not compilable API.

## Design in one minute

An application redraws a screen from current state for each logical update. Stable component IDs make the resulting UI tree comparable across renders. A button registers a stable, versioned action route and serializable payload; the executable handler is located again when a later Telegram update arrives, then runs only in a short-lived `ActionContext`. Durable effects are represented as typed events and handled by application services. Rendering and delivery remain separate so Telegram failures can be retried through an outbox without replaying arbitrary C++ callbacks.

```cpp
// Conceptual API; not implemented in D0.
void settings_screen(imtg::ViewContext& view) {
    view.ui().begin("settings");
    view.ui().text("Settings");
    view.ui().button("save", "Save",
        [](imtg::ActionContext& ctx) {
            ctx.dispatch(SaveRequested{});
        });
    view.ui().end();
}
```

All imtg functions and methods, including the public Immediate-Mode UI surface, use `snake_case` (`ui.button()`, `ui.input_text()`, `ctx.navigate()`). Types use `CamelCase`. Dear ImGui informs the programming model, not imtg's spelling convention.

## Architecture at a glance

```text
Telegram update -> session scheduler -> controller/view
                                      |         |
                                      v         v
                               typed events   UI tree
                                      |         |
                                      v         v
                              application model -> reconciler -> delivery adapter
                                      |                            |
                                      +-> storage/outbox <---------+
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

The next implementation PR should establish a deterministic, Telegram-free UI tree and callback runtime (M1), with tests for stable IDs, callback lifetime, event ordering, and reconciliation. See the roadmap for the full dependency chain.
