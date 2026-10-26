# Implementation status

**As of M1a:** the repository contains the first Telegram-free core foundation; UI rendering, action routing, scheduler, storage adapters, and delivery remain planned.

| Area | Status | Evidence / next proof |
|---|---|---|
| Architecture and contracts | `contract-only` | Markdown documents in `docs/`; review links and invariants |
| CMake `imtg_core` target and C++17 baseline | `implemented` | Root CMake target configures/builds; CTest executes offline core tests |
| Typed identities and deterministic clock | `implemented` | `include/imtg/core/`; M1a equality/encoding/hash tests |
| Immediate-Mode UI tree | `partial` | M1b implements immutable tree nodes, stable paths, RAII scopes, duplicate detection, and offline tests; components remain intentionally small |
| Callback actions and typed events | `partial` | M1c implements typed versioned action registry, plain injectable codec boundary, scoped context, restart reconstruction, and offline tests; Telegram authentication remains M3 |
| Controllers and application model | `planned` | M1d design review and Telegram-free test app |
| FSM and sessions | `planned` | M2 persistence/recovery tests |
| Storage contracts | `contract-only` | [STORAGE.md](contracts/STORAGE.md); no implementation |
| MDBX backend | `planned` | M2 conformance suite and adapter |
| Telegram transport / kurlyk | `planned` | M3 adapter tests against fakes and documented API limits |
| Render intents, reconciliation, and outbox | `planned` | M1d fake reconciliation; M2/M3 durable uncertain-outcome tests |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `deferred` | Introduce only with an implementation milestone |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
