# imtg

imtg is a C++17 framework foundation for Telegram applications whose views are
described with an Immediate-Mode UI model. It combines deterministic UI
descriptions with callback-based actions, typed events, controller-friendly
state, and replaceable persistence.

## Project status

The reviewed `main` tree contains the Telegram-free M1 foundation and the M2a--M2c
storage, session/FSM, reconciliation, outbox, and delivery-state baselines.
M2d-a adds an optional source-build MDBX adapter over the same backend-neutral
storage protocol. The library is experimental alpha software, not a production
Telegram framework.

Implemented surfaces include deterministic UI/action primitives, a bounded
reference runtime, snapshot/optimistic transactions, typed sessions and FSM
transitions, processed-update deduplication, fenced render-intent publication,
outbox reconciliation, and delivery acknowledgement contracts with reusable
offline conformance tests. Telegram transport, live payment adapters, Mini
App/ImGuiX UI, operator GUI, installed MDBX export, and process-crash or
migration recovery remain future work. See
[IMPLEMENTATION_STATUS.md](docs/IMPLEMENTATION_STATUS.md) for the authoritative
inventory.

## Design in one minute

An application redraws a screen from current state for each logical update.
Stable component IDs make the resulting UI tree comparable across renders. A
versioned action route binds a serializable payload to a short-lived
`ActionContext`; required changes use transactional commands and durable render
intents. Rendering and delivery remain separate so transport failures can be
retried through an outbox without replaying arbitrary C++ callbacks.

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

All imtg functions and methods, including the public Immediate-Mode UI surface,
use `snake_case` (`ui.button()`, `ui.input_text()`, `ctx.navigate()`). Types use
`CamelCase`. Dear ImGui informs the programming model, not imtg's spelling
convention.

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

The core owns contracts and deterministic orchestration. `kurlyk` is a planned
transport integration. MDBX through `mdbx-containers` is an optional
source-build adapter and is not a core dependency.

## Documentation map

- [Architecture](docs/ARCHITECTURE.md) -- boundaries and data flow.
- [Invariants](docs/INVARIANTS.md) -- rules that implementations must preserve.
- [Roadmap](docs/ROADMAP.md) -- dependency-gated milestones and acceptance criteria.
- [Implementation status](docs/IMPLEMENTATION_STATUS.md) -- what exists today.
- [Reference scenarios](docs/reference-scenarios.md) -- end-to-end design examples.
- [Sessions and FSM](docs/contracts/SESSIONS_AND_FSM.md) -- typed session and transactional transition contract.
- [Operator console](docs/contracts/OPERATOR_CONSOLE.md) -- optional `imtg-admin` boundary and delivery gates.
- [Engineering governance](docs/DOCUMENTATION_GOVERNANCE.md) -- how contracts stay canonical.
- [Coding style](docs/guides/coding-style.md) -- C++17, naming, ownership, and Doxygen rules.
- [Public API documentation](docs/guides/public-api-documentation.md) -- semantic coverage and strict validation.

Normative contracts live in [`docs/contracts/`](docs/contracts/). Significant
decisions are recorded in [`docs/adr/`](docs/adr/).

## Building

Keep all generated output under the ignored root `build/` directory:

```text
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Enable the provider-neutral commerce contracts with
`-DIMTG_BUILD_COMMERCE=ON`. MDBX builds require a clone with the two direct
submodules initialized; GitHub source archives do not contain submodule
payloads:

```text
git submodule update --init -- external/libmdbx external/mdbx-containers
cmake -S . -B build/mdbx -DIMTG_BUILD_MDBX=ON -DBUILD_TESTING=ON
cmake --build build/mdbx --config Debug
ctest --test-dir build/mdbx -C Debug --output-on-failure
```

The pinned revisions and non-recursive initialization procedure are documented
in [`external/README.md`](external/README.md). MDBX remains source-build-only
in this release: the installed package exports core and optional commerce
targets, but not `imtg::mdbx`.

Public headers and their implementations are co-located under
`src/imtg/<domain>/`, while consumer include paths remain `<imtg/...>`. Git
dependencies are pinned direct submodules under `external/<name>` and are
initialized non-recursively by default.

## Release scope

The proposed `v0.1.0-alpha.1` release freezes the reviewed Telegram-free
foundation and marks the MDBX adapter as experimental. See
[RELEASE_NOTES.md](RELEASE_NOTES.md) and
[`docs/release-smoke.md`](docs/release-smoke.md) for exact scope, limitations,
and verification procedures. The roadmap remains authoritative for subsequent
M2d-b, M2e, and M3 work.
