# Implementation status

Flat dependency wiring is owned by `external/CMakeLists.txt`: the two direct
pinned sibling submodules provide MDBX without recursive initialization or
configure-time downloads. The MDBX CTest configuration includes a Git inventory
check. See [dependency boundary](../external/README.md). This does not complete
M2d-b installed export or M2e crash recovery.

M2c-a, M2c-b, and M2c-c are merged in `main`. The current Draft PR starts
M2d-a: an optional persistent MDBX adapter over the accepted storage protocol.
M2d-b and M2e remain planned until the adapter and its cross-backend recovery
evidence are independently reviewed.

The M2d-a corrective adds explicit logical namespace routing into the six DBIs,
versioned binary key prefixes, isolated internal schema metadata, native-key
size admission and real M2b/M2c repository integration/reopen tests. Both the
MDBX target and header remain source-build-only. Scan result counts are bounded;
physical tombstone examination is not. This corrective is pending independent
review and does not establish process-crash durability.

The Sovetwave header-topology audit is documented in
[header-architecture.md](guides/header-architecture.md) and ADR 0009. The
base-domain nested umbrella paths remain installed; optional commerce now has
the root-level `imtg/commerce.hpp` when enabled, and Actions now has the
root-level `imtg/actions.hpp` with compatibility shims for the old nested
paths. Core/storage and the other remaining root-level umbrella migrations,
explicit install manifests, and compatibility transitions are still planned
implementation work.

**As of main merge `152e7e9` (PR #22 M2c-c delivery acknowledgement merged):** the repository contains the complete
Telegram-free M1 foundation, the M2a backend-neutral storage protocol, the M2b
typed session/FSM semantic baseline, and the M2c-a claims/fencing plus M2c-b
reconciliation/outbox slices. M2c-c now adds the merged reference delivery
lifecycle and acknowledgement boundary. The current Draft PR adds the first
optional MDBX persistence slice; process-crash recovery, package export and
transport remain planned for later milestones.

M1 acceptance gates are complete: deterministic rendering, typed/versioned actions, stale-handle rejection, per-conversation ordering, bounded admission and shutdown, observable worker failure, and dispatcher quiescence are covered by the offline test suite and the Debug/Release plus sanitizer CI matrix. M2a adds snapshot/optimistic transaction semantics, schema metadata, commit outcomes, and reusable conformance tests. M2b adds typed sessions, deterministic FSM transitions, processed-update deduplication, and atomic minimal render-intent publication. M2c-a claims/fencing and the M2c-b reconciliation/outbox slice are implemented; canonical `LT1` target scoping, applied-target desired-revision fencing, bounded physical identities, mandatory TX2 context/fencing, fail-closed staging, injected-repository recovery, and deterministic Unknown/interleaving tests are now covered. M2c-c adds the merged storage-only acknowledgement baseline, target reservations, a finite claim budget, typed `UnknownOutcome` success resolution, desired-state fencing at delivery admission, monotonic lease renewal, and deterministic late-success/TX1→TX2→TX1 regressions. Durable adapters and complete cross-backend recovery conformance remain open.

The Telegram application profile work is documentation-only planning. Typed
gateway routing, headless handlers, payment-priority handling, forms/tickets,
commerce runtime, moderation, durable jobs and the three reference applications
are not implemented; they are dependency-gated M3–M6 work described in the
research profiles and roadmap. The optional `imtg_commerce` target now contains
only the provider-neutral payment contract baseline; it does not process live
payments or provide a production adapter. The current corrective also makes
provider selection account- and allowlist-scoped, validates exact fiat/crypto
asset scales, binds authenticated evidence to payment identities, and exposes
an atomic payment/event transaction boundary. These are contract and test
baselines, not durable storage or live provider integration.

The optional `imtg-admin` operator console is also documentation-only. Its
ImGuiX-based UI and the GUI-shell ideas referenced from `mgc-platform` are not
dependencies of `imtg_core`; an authenticated operator API, authorization and
audit contract must be accepted before a separate GUI target is implemented.

| Area | Status | Evidence / next proof |
|---|---|---|
| Architecture and contracts | `contract-only` | Markdown documents in `docs/`; review links and invariants |
| Header topology | `Actions migrated; migration in progress` | [Header architecture guide](guides/header-architecture.md), [ADR 0009](adr/0009-domain-header-architecture.md); Actions snapshot `5dca497`, other domains remain transitional |
| CMake `imtg_core` target and C++17 baseline | `implemented` | Root CMake target configures/builds; CTest executes offline core tests |
| Typed identities and deterministic clock | `implemented` | `src/imtg/core/`; M1a equality/encoding/hash tests |
| Immediate-Mode UI tree | `partial` | M1b implements immutable tree nodes, stable paths, RAII scopes, duplicate detection, and offline tests; components remain intentionally small |
| Callback actions and typed events | `partial` | M1c implements typed versioned action registry, plain injectable codec boundary, scoped context, restart reconstruction, and offline tests; Telegram authentication remains M3 |
| Controllers and application model | `partial` | M1d reference runtime provides render/command callbacks; richer controller composition remains planned |
| Conversation scheduler and reference runtime | `partial` | M1d bounded worker pool with per-key serial mailboxes, transactional admission rollback, bounded queues, synchronous commands, fake reconciler, allocation-safe observable fail-stop status, bounded publication/shutdown and notification tests |
| FSM and sessions | `implemented` | [SESSIONS_AND_FSM.md](contracts/SESSIONS_AND_FSM.md); `src/imtg/sessions/`; `imtg_session_tests` covers encoding, transitions, deduplication, conflicts, and atomic TX1 |
| Storage protocol and in-memory backend | `implemented` | [STORAGE.md](contracts/STORAGE.md); `src/imtg/storage/storage.hpp`; fixture-based `imtg_storage_tests` conformance suite with revision, ABA, schema-gate, ownership, and commit-outcome coverage |
| Durable repositories and MDBX backend | `M2d-a draft` | Optional `imtg_mdbx` target, pinned `mdbx-containers`/`libmdbx`, six-DBI profile, shared backend-neutral conformance plus persistence/reopen tests; package export and crash proof remain M2d-b/M2e |
| Telegram transport / kurlyk | `planned` | M3 adapter tests against fakes and documented API limits |
| Payment provider contracts and capability audit | `partial` | [PAYMENTS.md](contracts/PAYMENTS.md), [PAYMENT_PROVIDER_CAPABILITY_AUDIT.md](research/PAYMENT_PROVIDER_CAPABILITY_AUDIT.md), optional `imtg_commerce` identities, account-scoped eligibility, exact asset-scale validation, evidence binding, shared `CommitResult` payment transactions, explicit refund policies and `imtg_payment_contract_tests`; provider adapters, durable persistence, webhooks and onboarding remain future work |
| Render intents, reconciliation, and outbox | `partial` | M2c-a claims/fencing, M2c-b canonical `LT1` target scoping, and M2c-c bounded delivery claims, dispatch admission, fenced acknowledgements, monotonic applied-target evidence, retry/Unknown policy, and supersession are merged; durable adapters and full cross-backend recovery remain future work |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `implemented` | CMake install/export package, Debug/Release CTest, and CI matrix |
| Operator console (`imtg-admin`) | `planned` | [Operator console contract](contracts/OPERATOR_CONSOLE.md) and [ADR 0008](adr/0008-operator-admin-console.md); no GUI source, transport, or dependency is present |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
