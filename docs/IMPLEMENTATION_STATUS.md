# Implementation status

The [architecture remediation plan](ARCHITECTURE_REMEDIATION_PLAN.md) orders
the 2026-11-21 audit work before further Telegram/webhook expansion. The
integration publication preserves original composition `467f6f46`; a dedicated
Linux Debug/Release CI job enables all modules simultaneously and checks the
flat boundary before configure. See [source and local evidence](research/M3_COMPOSITION_EVIDENCE_2026_11_21.md).
This does not mark the behavioral or structural remediation as complete.

Local integration validation combines outgoing #41 with mandatory logging #38.
The implementation uses LogIt++ privately in core/router, preserves semantic
error hooks and application logger ownership, and retains optional Telegram
modules. This note describes the isolated validation tree, not merged `main`.

The M3 Telegram entries in this document describe the optional Draft slices
under review; they do not claim that the current `main` has merged or released
Telegram support.

Flat dependency wiring is owned by `external/CMakeLists.txt` for MDBX and
`cmake/ImtgKurlykDependency.cmake` for the optional outgoing HTTP adapter.
Direct pinned siblings or parent-owned targets are used without recursive
initialization or configure-time downloads. The Git inventory check includes
the network profile; see [dependency boundary](../external/README.md). This
does not complete M2d-b installed export or M2e crash recovery.

M2c-a, M2c-b, and M2c-c are merged in `main`. M2d-a MDBX source-build
foundation and the flat dependency boundary are merged in PRs #24 and #26.
M2d-b and M2e remain planned until installed export and cross-backend recovery
evidence are independently reviewed.

The M2d-a corrective adds explicit logical namespace routing into the six DBIs,
versioned binary key prefixes, isolated internal schema metadata, native-key
size admission and real M2b/M2c repository integration/reopen tests. Both the
MDBX target and header remain source-build-only. Scan result counts are bounded;
physical tombstone examination is not. The merged source-build slice does not
establish process-crash durability.

The MDBX transaction-safety corrective adds rollback before propagating C++
write-operation exceptions and preserves the original absent/tombstone generation
for create-then-erase net-no-ops. Deterministic allocation-failure regressions
cover partial local staging and earlier unrelated writes; the shared InMemory/MDBX
conformance case covers repeated net-no-ops and compare-only watches. Persistent
record/schema formats and uncertain native commit behavior are unchanged.

On Windows Debug, CI run `44919903577` built successfully and passed tests 1-13,
but `imtg_mdbx_storage_tests` started at 15:44:00 UTC and did not finish before
job cancellation at 16:02:52 UTC. The cause is not established; this is not a passing
Debug result. The test now has a 180-second CTest timeout and flushed subtest,
operation and allocation-index progress outside allocation-injection scopes;
these diagnostics bound and localize a repeat without changing storage behavior
or weakening the allocation-failure regressions. MSVC Debug CRT assertion reports
go to captured stderr instead of a modal dialog; native assertion termination is
not disabled. No assertion is yet established as the cause of the observed hang.

The Sovetwave header-topology audit is documented in
[header-architecture.md](guides/header-architecture.md), ADR 0009, and the
accepted hybrid profile in [ADR 0011](adr/0011-hybrid-public-header-profile.md).
The main domains expose root-level umbrellas, with Telegram modules remaining optional.
All old installed leaf paths remain; the nested umbrellas forward to the root
entry points. The explicit manifest contains 29 core-package, seven optional
commerce, two optional Telegram, two generic Bot API transport and one
standalone kurlyk adapter entry points. Core does not include
optional adapters, commerce, or Telegram. Installed-header tests check each
enabled package inventory and every supported header as a first include.

Integration provenance for the current `main` is explicit: PR #24 merged as
`8abda4c`, PR #26 as `9e66a04`, PR #29 as `b03c778`, and PR #30 as `a1436bb`.
PR #25 and PR #28 were superseded and closed without direct merge commits.

**As of main merge `a1436bb` (PR #30):** the repository contains the complete
Telegram-free M1 foundation, the M2a backend-neutral storage protocol, the M2b
typed session/FSM semantic baseline, and the M2c-a claims/fencing plus M2c-b
reconciliation/outbox slices. M2c-c adds the merged reference delivery
lifecycle and acknowledgement boundary. M2d-a adds the optional source-build
MDBX persistence slice and pinned direct dependencies; process-crash recovery,
package export and transport remain planned for later milestones.

M1 acceptance gates are complete: deterministic rendering, typed/versioned actions, stale-handle rejection, per-conversation ordering, bounded admission and shutdown, observable worker failure, and dispatcher quiescence are covered by the offline test suite and the Debug/Release plus sanitizer CI matrix. M2a adds snapshot/optimistic transaction semantics, schema metadata, commit outcomes, and reusable conformance tests. M2b adds typed sessions, deterministic FSM transitions, processed-update deduplication, and atomic minimal render-intent publication. M2c-a claims/fencing and the M2c-b reconciliation/outbox slice are implemented; canonical `LT1` target scoping, applied-target desired-revision fencing, bounded physical identities, mandatory TX2 context/fencing, fail-closed staging, injected-repository recovery, and deterministic Unknown/interleaving tests are now covered. M2c-c adds the merged storage-only acknowledgement baseline, target reservations, a finite claim budget, typed `UnknownOutcome` success resolution, desired-state fencing at delivery admission, monotonic lease renewal, and deterministic late-success/TX1→TX2→TX1 regressions. Durable adapters and complete cross-backend recovery conformance remain open.

The Telegram application profile work remains staged planning beyond M3a.
Typed gateway routing and headless-handler admission are implemented in the
optional transport-neutral boundary; payment-priority handling, forms/tickets,
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
| Header topology | `implemented` | [Header architecture guide](guides/header-architecture.md), [ADR 0011](adr/0011-hybrid-public-header-profile.md); domain umbrellas and independently enabled optional components, explicit manifest, compatibility shims, topology CTest and installed first-include tests |
| CMake `imtg_core` target and C++17 baseline | `implemented` | Root CMake target configures/builds; CTest executes offline core tests |
| Typed identities and deterministic clock | `implemented` | `src/imtg/core/`; M1a equality/encoding/hash tests |
| Immediate-Mode UI tree | `partial` | M1b implements immutable tree nodes, stable paths, RAII scopes, duplicate detection, and offline tests; components remain intentionally small |
| Callback actions and typed events | `partial` | M1c implements typed versioned action registry, plain injectable codec boundary, scoped context, restart reconstruction, and offline tests; Telegram authentication remains M3 |
| Controllers and application model | `partial` | M1d reference runtime provides render/command callbacks; richer controller composition remains planned |
| Conversation scheduler and reference runtime | `partial` | M1d bounded worker pool with per-key serial mailboxes, transactional admission rollback, bounded queues, synchronous commands, fake reconciler, allocation-safe observable fail-stop status, bounded publication/shutdown and notification tests |
| FSM and sessions | `implemented` | [SESSIONS_AND_FSM.md](contracts/SESSIONS_AND_FSM.md); `src/imtg/sessions/`; `imtg_session_tests` covers encoding, transitions, deduplication, conflicts, and atomic TX1 |
| Storage protocol and in-memory backend | `implemented` | [STORAGE.md](contracts/STORAGE.md); `src/imtg/storage/storage.hpp`; fixture-based `imtg_storage_tests` conformance suite with revision, ABA, schema-gate, ownership, and commit-outcome coverage |
| Durable repositories and MDBX backend | `partial` | M2d-a optional source-build `imtg_mdbx` target, pinned `mdbx-containers`/`libmdbx`, six-DBI profile, shared conformance and persistence/reopen tests; package export and crash proof remain M2d-b/M2e |
| Telegram gateway / transport | `partial` | Optional `imtg::telegram` typed update boundary, generic `imtg::telegram_transport` request/result validation, and standalone `imtg::telegram_kurlyk` outgoing HTTP adapter; its positive request-body bound cannot exceed `LONG_MAX`. Fake and real loopback tests cover limits, malformed/lost responses, cancellation, no retry and isolated request groups; installed consumer, parent-target reuse and headers are checked. Worker/outbox orchestration, incoming webhook, durable inbox and live Telegram remain unimplemented |
| Payment provider contracts and capability audit | `partial` | [PAYMENTS.md](contracts/PAYMENTS.md), [PAYMENT_PROVIDER_CAPABILITY_AUDIT.md](research/PAYMENT_PROVIDER_CAPABILITY_AUDIT.md), optional `imtg_commerce` identities, account-scoped eligibility, exact asset-scale validation, evidence binding, shared `CommitResult` payment transactions, explicit refund policies and `imtg_payment_contract_tests`; provider adapters, durable persistence, webhooks and onboarding remain future work |
| Render intents, reconciliation, and outbox | `partial` | M2c-a claims/fencing, M2c-b canonical `LT1` target scoping, and M2c-c bounded delivery claims, dispatch admission, fenced acknowledgements, monotonic applied-target evidence, retry/Unknown policy, and supersession are merged; durable adapters and full cross-backend recovery remain future work |
| Localization | `planned` | M4 catalog, fallback, and pluralization tests |
| Users, chats, history, access control, jobs | `planned` | M4/M5 service and retention tests |
| Build targets, releases, CI | `implemented` | CMake install/export package, Debug/Release CTest, and CI matrix |
| Operator console (`imtg-admin`) | `planned` | [Operator console contract](contracts/OPERATOR_CONSOLE.md) and [ADR 0008](adr/0008-operator-admin-console.md); no GUI source, transport, or dependency is present |

README sketches and this design do not imply a working build. This table must be updated whenever a later PR changes implementation coverage.
