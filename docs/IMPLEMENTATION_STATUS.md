# Implementation status

**As of merged M1:** the repository contains the complete Telegram-free M1 foundation, including deterministic UI/action primitives and a bounded reference runtime. The five stacked M1 PRs are merged into `main`; production persistence, delivery, and transport remain planned for later milestones.

M1 acceptance gates are complete: deterministic rendering, typed/versioned actions, stale-handle rejection, per-conversation ordering, bounded admission and shutdown, observable worker failure, and dispatcher quiescence are covered by the offline test suite and the Debug/Release plus sanitizer CI matrix. M2 remains design-only until its storage protocol and recovery contracts are accepted.

| Area | Status | Evidence / next proof |
|---|---|---|
| Architecture and contracts | `contract-only` | Markdown documents in `docs/`; review links and invariants |
| CMake `imtg_core` target and C++17 baseline | `implemented` | Root CMake target configures/builds; CTest executes offline core tests |
| Typed identities and deterministic clock | `implemented` | `include/imtg/core/`; M1a equality/encoding/hash tests |
| Immediate-Mode UI tree | `partial` | M1b implements immutable tree nodes, stable paths, RAII scopes, duplicate detection, and offline tests; components remain intentionally small |
| Callback actions and typed events | `partial` | M1c implements typed versioned action registry, plain injectable codec boundary, scoped context, restart reconstruction, and offline tests; Telegram authentication remains M3 |
| Controllers and application model | `partial` | M1d reference runtime provides render/command callbacks; richer controller composition remains planned |
| Conversation scheduler and reference runtime | `partial` | M1d bounded worker pool with per-key serial mailboxes, transactional admission rollback, bounded queues, synchronous commands, fake reconciler, allocation-safe observable fail-stop status, bounded publication/shutdown and notification tests |
| FSM and sessions | `planned` | M2 persistence/recovery tests |
| Storage contracts | `contract-only` | [STORAGE.md](contracts/STORAGE.md); no implementation |
| MDBX backend | `planned` | M2 conformance suite and adapter |
| Telegram transport / kurlyk | `planned` | M3 adapter tests against fakes and documented API limits |
| Render intents, reconciliation, and outbox | `planned` | M1d fake reconciliation; M2/M3 durable uncertain-outcome tests |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `implemented` | CMake install/export package, Debug/Release CTest, and CI matrix |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
