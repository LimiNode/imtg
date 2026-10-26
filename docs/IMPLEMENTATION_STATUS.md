# Implementation status

**As of D0:** the repository contains documentation only.

| Area | Status | Evidence / next proof |
|---|---|---|
| Architecture and contracts | `contract-only` | Markdown documents in `docs/`; review links and invariants |
| Immediate-Mode UI tree | `planned` | M1 must add deterministic tree tests |
| Callback actions and typed events | `planned` | M1 contract tests for scope and dispatch |
| Controllers and application model | `planned` | M1 design review and Telegram-free test app |
| FSM and sessions | `planned` | M2 persistence/recovery tests |
| Storage contracts | `contract-only` | [STORAGE.md](contracts/STORAGE.md); no implementation |
| MDBX backend | `planned` | M2 conformance suite and adapter |
| Telegram transport / kurlyk | `planned` | M3 adapter tests against fakes and documented API limits |
| Render intents, reconciliation, and outbox | `planned` | M2 recovery/fencing and M3 uncertain-outcome tests |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `deferred` | Introduce only with an implementation milestone |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
