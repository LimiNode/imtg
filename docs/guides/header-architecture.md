# Header architecture and dependency audit

**Status:** normative for new work; staged implementation migration is planned.
**Scope:** `core`, `actions`, `storage`, `sessions`, `reconciliation`, `ui`,
`runtime`, and the optional `commerce` addendum.

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

## Audit snapshots

The base-domain inventory below is measured against the immutable PR #19 merge
`6a0814c8107a14d60cafcbdf44f1711eec446b2c`. The commerce addendum is measured
against the restacked PR #17 source snapshot `574c448` (the same commerce
source that was reviewed at `fb5754d1a081ad8a4bc184a4bd26043137126c39`). The
Actions implementation addendum is measured against source snapshot
`5dca4977adcbe82d1d897f95fae793c058c9322c`; this snapshot includes the
standalone validation and registration publication-order correctives. These
snapshots are explicit so the audit does not silently change with a future
`main`.

## Audit of the current tree

The PR #19 base snapshot predates the umbrella migration. The existing nested
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

### Commerce addendum

At the commerce snapshot, the optional domain has the following public
surface:

| Domain | Current umbrella | Current public component headers | Direct domain dependencies |
| --- | --- | --- | --- |
| `commerce` | `imtg/commerce.hpp` (optional) | `PaymentTypes.hpp`, `PaymentPolicy.hpp`, `PaymentRecords.hpp`, `PaymentTransaction.hpp`, `PaymentProvider.hpp`, `PaymentServices.hpp` | `core`, `storage` |

The six component headers are ordered by the umbrella as value types, policy,
records, transaction, provider, and services. `payments.cpp` is an
implementation unit and is not installed. The concrete include edges are:

```text
commerce/PaymentTypes.hpp       -> core component headers
commerce/PaymentTransaction.hpp -> storage/storage.hpp
```

Until `core.hpp` and `storage.hpp` exist, these direct includes of the
standalone lower-level headers are an explicitly allowed transitional
dependency. A later core/storage migration must replace them with the root
entry points and re-run the commerce boundary tests.

The commerce install boundary is configuration-dependent: a core-only install
excludes `imtg/commerce.hpp` and `imtg/commerce/`; a commerce-enabled install
adds the umbrella and the six documented component headers. The removed
`imtg/commerce/payments.hpp` is not a compatibility API.

### Actions implementation addendum

The Actions migration now provides the root umbrella `imtg/actions.hpp` and
these standalone public contracts:

| Header | Ownership |
| --- | --- |
| `imtg/actions/ActionCommand.hpp` | command value emitted by handlers |
| `imtg/actions/ActionContext.hpp` | invocation-scoped dispatch capability |
| `imtg/actions/PayloadCodec.hpp` | typed payload encode/decode contract |
| `imtg/actions/ActionHandle.hpp` | generation-bound typed capability |
| `imtg/actions/ActionRegistry.hpp` | registration, validation, token creation, invocation |
| `imtg/actions/token.hpp` | external token identities and codecs |

The split is by responsibility rather than by every DTO. Registration,
generation fencing, stale-handle behavior, callback lifetime, route validation,
and synchronization are unchanged. `ActionRegistry.cpp` is beside its owning
header. The root umbrella still includes the existing
`<imtg/core/core.hpp>` transitional lower-level umbrella because
`<imtg/core.hpp>` does not exist yet.

The installed nested paths `imtg/actions/actions.hpp` and
`imtg/actions/registry.hpp` remain thin compatibility shims for consumers of
the current package. They are not additional API surfaces; removal requires a
declared compatibility-breaking release and migration note, so the shims are
not an indefinite parallel implementation.

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
| `commerce.hpp` (optional) | `<imtg/core.hpp>`, `<imtg/storage.hpp>` after those umbrellas land | `commerce/PaymentTypes.hpp`, `PaymentPolicy.hpp`, `PaymentRecords.hpp`, `PaymentTransaction.hpp`, `PaymentProvider.hpp`, `PaymentServices.hpp` |

The exact component order is part of each implementation PR and must be stable,
acyclic, and tested. A component that is intentionally standalone may include
the lower-level public headers it needs directly; it must not rely on an
unrelated consumer having included an umbrella first.

## Public entry points and migration

The intended mapping is:

| Current installed path | Target entry point | Migration note |
| --- | --- | --- |
| `imtg/core/core.hpp` | `imtg/core.hpp` | relocate umbrella after dependency audit |
| `imtg/actions/actions.hpp` | `imtg/actions.hpp` | root umbrella implemented; nested path is a compatibility shim |
| no domain umbrella | `imtg/storage.hpp` | add only if storage remains a meaningful public boundary |
| no domain umbrella | `imtg/sessions.hpp` | add after session/render-intent ownership is confirmed |
| no domain umbrella | `imtg/reconciliation.hpp` | add after outbox boundary is confirmed |
| `imtg/ui/ui.hpp` | `imtg/ui.hpp` | relocate umbrella |
| `imtg/runtime/runtime.hpp` | `imtg/runtime.hpp` | relocate umbrella |
| `imtg/commerce.hpp` (optional) | `imtg/commerce.hpp` | already at the target root; keep excluded from core-only installs |

Component leaf paths such as `imtg/core/identifiers.hpp` and
`imtg/sessions/session.hpp` remain the owning public paths in the target model;
the migration changes domain entry points, not the location of a leaf unless a
separate ownership review explicitly says otherwise.

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

The optional `commerce` domain has already adopted its root umbrella in the
payment-contract change. It is not part of the Actions/core migration, and its
transitional lower-level includes remain explicitly documented above until the
corresponding core/storage umbrellas land.

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

