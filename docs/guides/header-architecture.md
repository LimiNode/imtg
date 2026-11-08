# Header architecture and dependency audit

**Status:** normative for new work; staged implementation migration is planned.
**Scope:** `core`, `actions`, `storage`, `sessions`, `reconciliation`, `ui`, and
`runtime`. The optional commerce domain is governed by its own change and is
not modified by this audit.

This guide records the current include topology and the target hierarchical
umbrella model adopted from the Sovetwave C++ engineering guidance. It is a
dependency contract, not permission to perform a repository-wide mechanical
rename. Structural migrations are delivered as small, independently tested
implementation PRs.

## Rules

1. A complete domain owns one public umbrella at
   `src/imtg/<domain>.hpp`. Domain implementation headers and non-template
   sources live under `src/imtg/<domain>/`.
2. An umbrella includes its lower-level domain entry points first and then its
   own public components in a deterministic order. It is the supported way to
   consume a domain as a unit.
3. The domain dependency graph is a DAG. A domain must not include an
   umbrella or leaf from a domain above it, and an umbrella must not include
   itself through any path.
4. A leaf never includes its own domain umbrella. A public standalone header is
   self-contained and may be included first in a translation unit.
5. A private leaf may rely on the context established by its umbrella only when
   that fact is explicitly documented. Private leaves are not installed or
   tested as standalone public API.
6. Cross-domain includes use an installed public entry point such as
   `<imtg/core.hpp>`. Intra-domain includes use local paths such as
   `"session.hpp"`; `..` relative includes are forbidden.
7. Public include paths describe contracts, not the physical implementation
   layout. The install manifest names the supported umbrellas and standalone
   headers explicitly; implementation-only headers are excluded.
8. A meaningful public type normally owns one `CamelCase.hpp` header. Related
   value types may remain together when splitting them would create artificial
   coupling or a DTO-only file explosion. Umbrellas and utility headers use
   the documented `snake_case.hpp` convention.
9. Template definitions belong in the owning header or an intentionally paired
   `.tpp`/`.ipp` file. Non-template `.cpp` files remain beside their owning
   header.

## Audit of the current tree

The current `main` predates the umbrella migration. The existing nested
`<imtg>/<domain>/...` paths are installed and therefore are current public
paths. No path is removed by this documentation change.

| Domain | Current umbrella | Current public component headers | Direct domain dependencies |
| --- | --- | --- | --- |
| `core` | `imtg/core/core.hpp` | `clock.hpp`, `identifiers.hpp`, `result.hpp` | none |
| `actions` | `imtg/actions/actions.hpp` | `registry.hpp`, `token.hpp` | `core` |
| `storage` | none | `storage.hpp`, `time_encoding.hpp` | `core` |
| `sessions` | none | `fsm.hpp`, `session.hpp`, `render_intents.hpp` | `core`, `storage` |
| `reconciliation` | none | `outbox.hpp` | `core`, `sessions` |
| `ui` | `imtg/ui/ui.hpp` | `tree.hpp` | `core`, `actions` |
| `runtime` | `imtg/runtime/runtime.hpp` | `reference_runtime.hpp` | `core`, `actions`, `ui` |

The current graph, where an arrow means “depends on”, is:

```text
actions ───────▶ core
storage ───────▶ core
sessions ──────▶ storage ─────▶ core
reconciliation ▶ sessions
ui ────────────▶ actions
runtime ───────▶ ui ───────────▶ actions ─────▶ core
```

`reconciliation` also uses core directly. `sessions/render_intents.hpp` uses
the session contract within the same domain; this is not a cross-domain cycle.
The observed cross-domain graph is acyclic, but its public entry points are
inconsistent and several domains have no umbrella at all.

The current CMake install recursively installs every `.hpp`, `.tpp`, and
`.ipp` below `src/imtg`. Consequently, every current header is effectively a
public installed header; there is no private-header install boundary today.
`tests/public_headers` compiles each installed header first, which catches
missing direct includes but does not yet verify umbrella ordering or the
absence of private headers from the package.

There are therefore no classified private header leaves in the current tree:
the `.cpp` files are implementation units, while every `.hpp` is installed.
Future implementation PRs must classify a leaf as public standalone or private
before changing the explicit install manifest.

## Target topology

The target entry points are root-level umbrellas:

```text
src/imtg/
├── core.hpp
├── actions.hpp
├── storage.hpp
├── sessions.hpp
├── reconciliation.hpp
├── ui.hpp
├── runtime.hpp
├── core/             # owned leaves and .cpp files
├── actions/
├── storage/
├── sessions/
├── reconciliation/
├── ui/
└── runtime/
```

The intended topological order is:

```text
core
  ├── actions
  │     └── ui
  │           └── runtime
  └── storage
        └── sessions
              └── reconciliation
```

This is a partial order, not a requirement that all domains be merged into one
header. A target umbrella may include only the lower-level umbrellas and its
own declared public components. The proposed include sets are:

| Target umbrella | Lower-level umbrella includes | Own component includes |
| --- | --- | --- |
| `core.hpp` | none | `core/clock.hpp`, `core/identifiers.hpp`, `core/result.hpp` |
| `actions.hpp` | `<imtg/core.hpp>` | `actions/token.hpp`, `actions/registry.hpp` |
| `storage.hpp` | `<imtg/core.hpp>` | `storage/time_encoding.hpp`, `storage/storage.hpp` |
| `sessions.hpp` | `<imtg/core.hpp>`, `<imtg/storage.hpp>` | `sessions/fsm.hpp`, `sessions/session.hpp`, `sessions/render_intents.hpp` |
| `reconciliation.hpp` | `<imtg/core.hpp>`, `<imtg/sessions.hpp>` | `reconciliation/outbox.hpp` |
| `ui.hpp` | `<imtg/core.hpp>`, `<imtg/actions.hpp>` | `ui/tree.hpp` |
| `runtime.hpp` | `<imtg/core.hpp>`, `<imtg/actions.hpp>`, `<imtg/ui.hpp>` | `runtime/reference_runtime.hpp` |

The exact component order is part of each implementation PR and must be stable,
acyclic, and tested. A component that is intentionally standalone may include
the lower-level public headers it needs directly; it must not rely on an
unrelated consumer having included an umbrella first.

## Public entry points and migration

The intended mapping is:

| Current installed path | Target entry point | Migration note |
| --- | --- | --- |
| `imtg/core/core.hpp` | `imtg/core.hpp` | relocate umbrella after dependency audit |
| `imtg/actions/actions.hpp` | `imtg/actions.hpp` | relocate umbrella; review registry split separately |
| no domain umbrella | `imtg/storage.hpp` | add only if storage remains a meaningful public boundary |
| no domain umbrella | `imtg/sessions.hpp` | add after session/render-intent ownership is confirmed |
| no domain umbrella | `imtg/reconciliation.hpp` | add after outbox boundary is confirmed |
| `imtg/ui/ui.hpp` | `imtg/ui.hpp` | relocate umbrella |
| `imtg/runtime/runtime.hpp` | `imtg/runtime.hpp` | relocate umbrella |

The old nested paths remain available until an implementation PR makes an
explicit compatibility decision. That PR must identify which paths were
promised to consumers, choose a versioned removal or a time-bounded forwarding
shim, update the installed-header tests, and document the migration. Forwarding
headers are not an indefinite second API.

The actions registry is intentionally not split by this audit. `registry.hpp`
currently contains several related public contracts; any split into
`ActionRegistry.hpp`, `ActionContext.hpp`, or codec/handle headers requires a
separate dependency and API review that preserves lifetime, generation fencing,
stale-handle, route-validation, and synchronization semantics.

The optional `commerce` domain is outside this migration. Its umbrella and
standalone headers are reviewed in the payment-contract change and must remain
independently optional from the core-only build.

## Verification contract

Each structural implementation PR must update the install manifest and prove:

- every documented umbrella compiles as the first include in a clean
  translation unit;
- every documented standalone public header does the same;
- deterministic include ordering does not create a cycle or upward dependency;
- installed packages contain no private leaves;
- compatibility paths, if retained, are covered by an explicit migration test;
- core-only and optional-domain configurations preserve their dependency
  boundaries.

The current audit itself is documentation-only. Its checks are Markdown-link
validation and `git diff --check`; no source layout, install manifest, or API
behavior is changed here.

