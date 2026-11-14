# Implementation status

M2c-a, M2c-b, and M2c-c are merged in `main` at `152e7e9`. M2d-a is being
reviewed separately in Draft PR #24; M2d-b and M2e remain planned.

The hybrid public-header profile is accepted in
[ADR 0011](adr/0011-hybrid-public-header-profile.md): domain umbrellas plus
standalone components and independently enabled optional modules. This policy
is implemented by this slice's root umbrellas and explicit install manifest;
independent review remains required.

The Sovetwave header-topology audit is documented in
[header-architecture.md](guides/header-architecture.md) and ADR 0009. The
eight domains expose root-level umbrellas. All old installed leaf paths remain;
the nested umbrellas forward to the root entry points. The explicit manifest
contains 29 core-package and seven optional commerce entry points. Core does not
include optional adapters or commerce. Installed-header tests check the exact
package inventory and every supported header as a first include.

**As of main merge `152e7e9` (PR #22 M2c-c merged):** the repository contains the complete
Telegram-free M1 foundation, the M2a backend-neutral storage protocol, the M2b
typed session/FSM semantic baseline, and the M2c-a claims/fencing plus M2c-b
reconciliation/outbox slices and the M2c-c reference
delivery lifecycle; durable process-crash persistence and transport remain
planned for later milestones.

M1 acceptance gates are complete: deterministic rendering, typed/versioned actions, stale-handle rejection, per-conversation ordering, bounded admission and shutdown, observable worker failure, and dispatcher quiescence are covered by the offline test suite and the Debug/Release plus sanitizer CI matrix. M2a adds snapshot/optimistic transaction semantics, schema metadata, commit outcomes, and reusable conformance tests. M2b adds typed sessions, deterministic FSM transitions, processed-update deduplication, and atomic minimal render-intent publication. M2c-a claims/fencing and the M2c-b reconciliation/outbox slice are implemented; canonical `LT1` target scoping, applied-target desired-revision fencing, bounded physical identities, mandatory TX2 context/fencing, fail-closed staging, injected-repository recovery, and deterministic Unknown/interleaving tests are now covered. M2c-c adds the merged storage-only acknowledgement baseline, target reservations, and a finite claim budget. The merged corrective also adds typed `UnknownOutcome` success resolution, desired-state fencing at delivery admission, monotonic lease renewal, and deterministic late-success/TX1→TX2→TX1 regressions. Durable adapters and complete cross-backend recovery conformance remain open.

The Telegram application profile work is documentation-only planning. Typed
gateway routing, headless handlers, payment-priority handling, forms/tickets,
commerce runtime, moderation, durable jobs and the three reference applications
are not implemented; they are dependency-gated M3–M6 work described in the
research profiles and roadmap. The optional `imtg_commerce` target now contains
only the provider-neutral payment contract baseline; it does not process live
payments or provide a production adapter. The merged commerce corrective also makes
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
| Header topology | `hybrid migration implemented; review pending` | [Header architecture guide](guides/header-architecture.md), [ADR 0011](adr/0011-hybrid-public-header-profile.md); eight root umbrellas, explicit manifest, compatibility shims, topology CTest and installed first-include tests |
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
| Payment provider contracts and capability audit | `partial` | [PAYMENTS.md](contracts/PAYMENTS.md), [PAYMENT_PROVIDER_CAPABILITY_AUDIT.md](research/PAYMENT_PROVIDER_CAPABILITY_AUDIT.md), optional `imtg_commerce` identities, account-scoped eligibility, exact asset-scale validation, evidence binding, shared `CommitResult` payment transactions, explicit refund policies and `imtg_payment_contract_tests`; provider adapters, durable persistence, webhooks and onboarding remain future work |
| Render intents, reconciliation, and outbox | `partial` | M2c-a claims/fencing and M2c-b canonical `LT1` target scoping, mandatory TX2 context, authoritative session/generation/applied-target fencing, atomic outbox publication, fail-closed borrowed staging, and conservative Unknown recovery are merged; M2c-c also provides bounded delivery claims, dispatch admission, fenced acknowledgements, monotonic applied-target evidence, retry/Unknown policy, and supersession; durable adapters and full cross-backend recovery remain future work |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `implemented` | CMake install/export package, Debug/Release CTest, and CI matrix |
| Operator console (`imtg-admin`) | `planned` | [Operator console contract](contracts/OPERATOR_CONSOLE.md) and [ADR 0008](adr/0008-operator-admin-console.md); no GUI source, transport, or dependency is present |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
