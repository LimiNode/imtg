# Implementation status

**As of M2b:** the repository contains the complete Telegram-free M1 foundation, the M2a backend-neutral storage protocol, and the M2b typed session/FSM semantic baseline. The five stacked M1 PRs plus Repository Layout and M2a are merged into `main`; durable process-crash persistence, delivery, and transport remain planned for later milestones.

M1 acceptance gates are complete: deterministic rendering, typed/versioned actions, stale-handle rejection, per-conversation ordering, bounded admission and shutdown, observable worker failure, and dispatcher quiescence are covered by the offline test suite and the Debug/Release plus sanitizer CI matrix. M2a adds snapshot/optimistic transaction semantics, schema metadata, commit outcomes, and reusable conformance tests. M2b adds typed sessions, deterministic FSM transitions, processed-update deduplication, and atomic minimal render-intent publication; M2c–M2e remain planned.

| Area | Status | Evidence / next proof |
|---|---|---|
| Architecture and contracts | `contract-only` | Markdown documents in `docs/`; review links and invariants |
| CMake `imtg_core` target and C++17 baseline | `implemented` | Root CMake target configures/builds; CTest executes offline core tests |
| Typed identities and deterministic clock | `implemented` | `src/imtg/core/`; M1a equality/encoding/hash tests |
| Immediate-Mode UI tree | `partial` | M1b implements immutable tree nodes, stable paths, RAII scopes, duplicate detection, and offline tests; components remain intentionally small |
| Callback actions and typed events | `partial` | M1c implements typed versioned action registry, plain injectable codec boundary, scoped context, restart reconstruction, and offline tests; Telegram authentication remains M3 |
| Controllers and application model | `partial` | M1d reference runtime provides render/command callbacks; richer controller composition remains planned |
| Conversation scheduler and reference runtime | `partial` | M1d bounded worker pool with per-key serial mailboxes, transactional admission rollback, bounded queues, synchronous commands, fake reconciler, allocation-safe observable fail-stop status, bounded publication/shutdown and notification tests |
| FSM and sessions | `implemented` | [SESSIONS_AND_FSM.md](contracts/SESSIONS_AND_FSM.md); `src/imtg/sessions/`; `imtg_session_tests` covers encoding, transitions, deduplication, conflicts, and atomic TX1 |
| Storage protocol and in-memory backend | `implemented` | [STORAGE.md](contracts/STORAGE.md); `src/imtg/storage/storage.hpp`; fixture-based `imtg_storage_tests` conformance suite with revision, ABA, schema-gate, ownership, and commit-outcome coverage |
| Durable repositories and MDBX backend | `planned` | M2c–M2e durable claims, recovery, MDBX adapter, and process-crash proof |
| Telegram transport / kurlyk | `planned` | M3 adapter tests against fakes and documented API limits |
| Render intents, reconciliation, and outbox | `partial` | M2b minimal atomic render-intent record; M2c/M3 claim, reconciliation, outbox, and delivery tests |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `implemented` | CMake install/export package, Debug/Release CTest, and CI matrix |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
