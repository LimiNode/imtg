# Roadmap

Milestones are dependency-gated; independent research may proceed in parallel,
but acceptance evidence is required before declaring a milestone complete.

## Dependency map

```text
                         +--> M4 (composition/localization) -+
D0 --> M1 --> M2 -------+                                    +--> M5 --> M6
  \       \             +--> M3 (delivery/gateway) -----------+
   \       +--> locale contract/tests may proceed after M1
    +--> governance/contracts (gate all implementation)
```

The graph shows implementation blockers, not a mandatory serial work queue. M4
contract/test prototyping may proceed after M1, while durable M4 services
depend on M2. M3 depends on M2 for outbox/session revisions and may begin with
one small Telegram example before every optional application module exists. M5
depends on storage and delivery contracts but does not block M4 localization
work. Application capability research is recorded in
[TELEGRAM_APPLICATION_CAPABILITY_AUDIT.md](research/TELEGRAM_APPLICATION_CAPABILITY_AUDIT.md)
and the three integration profiles in
[TELEGRAM_APPLICATION_PROFILES.md](research/TELEGRAM_APPLICATION_PROFILES.md).
Payment-provider evidence and the provider-neutral contract are recorded in
[PAYMENT_PROVIDER_CAPABILITY_AUDIT.md](research/PAYMENT_PROVIDER_CAPABILITY_AUDIT.md)
and [PAYMENTS.md](contracts/PAYMENTS.md).

## D0 — Architecture & Engineering Foundation

**Scope:** contracts, invariants, governance, ADR, scenarios, roadmap, and
honest status. **Dependencies:** none. **Deliverables:** this documentation
layer. **Acceptance:** links and terminology are consistent; examples are
marked conceptual; `git diff --check` passes. **Tests:** documentation review
and link/status audit. **Exit:** a new agent can identify ownership, limits,
and next milestone. **D0 restrictions:** do not add production code, CMake
targets, dependencies, submodules, or adapters in this milestone; keep the PR
documentation-first and do not merge it without explicit approval.

## M1 — Typed UI tree, callbacks, controllers, deterministic core

M1 is delivered as independently reviewable stacked PRs: **M1a** foundation
and typed identities, **M1b** deterministic UI tree, **M1c** action
registry/callback semantics, and **M1d** scheduler/reference runtime. Each PR
must build and test against its declared base; the M1 milestone is complete
only after all four acceptance gates pass.

M1 freezes the transport-neutral node vocabulary, immutable render snapshots,
stable paths, typed versioned routes, scoped callbacks, restart
reconstruction, and bounded per-key scheduling. Telegram formatting,
authentication and transport remain deferred to M3. M1 uses the accepted
`ConversationKey` (`bot_id + chat_id + Chat` or `UserInChat`).

**Acceptance:** repeated renders are deterministic; malformed/unknown/stale
callbacks are rejected; callbacks cannot escape scope; route handlers resolve
after restart; per-key ordering and bounded admission are tested. **Exit:** M2
can persist session contracts without changing UI semantics.

## M2 — FSM, sessions, storage, reconciliation and MDBX boundary

**M2a status:** implemented. `IStorageBackend`, `ITransaction`, `UnitOfWork`,
fail-closed schema metadata, monotonic expected revisions with delete/recreate
ABA protection, deterministic commit outcomes, and the in-memory
snapshot/optimistic backend are available. This is a semantic baseline, not a
physical durability guarantee.

**M2b status:** implemented on the in-memory semantic baseline. Typed session
records, guarded FSM transitions, timeout/cancellation/terminal events,
processed-update deduplication, optimistic revisions and atomic minimal
render-intent publication are covered by tests.

**M2c-a status:** merged in `main`; claims, leases and fencing are implemented
on the reference backend. **M2c-b status:** reconciliation/outbox planning is
merged in `main` on the backend-neutral in-memory baseline. Its gates are
fully fenced publishable TX2 plans, fail-closed borrowed staging, canonical
target scoping and conservative Unknown recovery. **M2c-c status:** the current
Draft implementation adds bounded outbox delivery claims, the committed
`DispatchAdmitted` boundary, fenced success/failure/Unknown transitions,
monotonic applied-target acknowledgements, and explicit supersession. It remains
an in-memory semantic baseline and makes no process-crash durability or Telegram
transport claim. Independent review and merge of this slice are still pending.

**M2d/M2e:** optional MDBX adapter and cross-backend conformance/recovery
proof. They depend on the accepted transaction/recovery contracts and must not
create a DBI per user or chat.

**Header architecture track:** the Sovetwave domain-umbrella audit and rules
are documented in [header-architecture.md](guides/header-architecture.md) and
[ADR 0009](adr/0009-domain-header-architecture.md). Root-level umbrella
migration and explicit install-boundary changes are separate, staged
implementation PRs; this track does not change M2 behavior or claim that the
migration is complete.

**M2 acceptance:** in-memory and MDBX conformance covers schema compatibility,
atomic multi-record commit, duplicate updates, revision conflicts, TX1/TX2
crash points, stale fencing, uncertain commit, interrupted migration, bounded
scans and shutdown with pending work. Telegram calls and cross-database
atomicity remain deferred.

## M3 — Telegram gateway and delivery

Add the verified Telegram boundary for typed message, edited-message,
callback-query, chat-member, join-request and payment updates; declarative
filters/middleware; headless and interactive handlers; permission checks;
priority/deadline handling for payment pre-checkout; kurlyk-backed transport;
desired/applied revisions; retries, rate limits, bounded admission and
uncertain outcomes. Add the external payment webhook boundary without
claiming a provider adapter. Applications enable only the update classes they
need.

**Depends on:** M2 and verified official Telegram API behavior.
**Acceptance:** fake transport tests cover send/edit/delete/callback-answer,
429/retry, authorization, duplicate updates, queue overload and
`UnknownOutcome`; a headless moderation handler and an urgent payment handler
run without a UI render intent; shutdown leaves recoverable outbox work.
**Exit:** one minimal bot runs with documented operational limits.
**Deferred:** advanced webhooks, Mini Apps and domain-specific
commerce/moderation policy.

## M4 — Application composition and localization

Add immutable catalogs, fallback/plural rules and locale snapshots; module
composition and dependency injection; optional typed forms/dialogs,
draft/resume storage, attachment metadata and reusable policy hooks; users,
chats and permission contracts; and provider-neutral payment method and
commercial integration-model selection for checkout UI. The payment provider
capability audit remains the onboarding source of truth; gateway,
hosted-checkout, self-hosted processor, marketplace/reseller and
Merchant-of-Record paths are not interchangeable.

**Depends on:** M1; durable services depend on M2/M3. **Acceptance:**
concurrent users render different locales without ID changes; forms validate
and resume after restart; a headless module and an interactive module share one
router and bounded scheduler; missing keys and plural categories have
deterministic fallbacks. **Exit:** `intake-bot` contract tests compose forms,
tickets and notifications without putting ticket fields in imtg core.
**Deferred:** domain-specific catalog, CRM and advanced history policy.

## M5 — Durable domain services and reference applications

Add optional durable users/chats/permissions; moderation journals and
reversible sanctions; bounded durable jobs/TTL; attachment retention/deletion;
ticket persistence and threaded support; commerce catalog/order/payment/
fulfillment primitives; provider adapters selected by merchant eligibility;
payment links, hosted checkout, self-hosted crypto settlement, webhooks,
refunds and reconciliation; separately contracted marketplace/reseller and
Merchant-of-Record integrations;
moderation and commerce adapters; and the `intake-bot`, `shop-bot` and
`guardian-bot` reference applications.

**Depends on:** M2–M4 and a verified delivery contract. **Acceptance:**
storage conformance and restart/recovery tests pass; duplicate payment and
unique-item allocation are safe; ticket and moderation transitions are fenced
and audited; anti-raid simulations remain bounded; each reference application
exercises shared routing, authorization, localization, scheduling and effect
recovery. **Exit:** application modules remain optional and independently
buildable. **Deferred:** provider-specific CRM, sophisticated inventory and AI
policy.

## M6 — Advanced integration and operations

Add production webhook operations, metrics and operational dashboards;
extension modules; load-shedding and capacity planning; raid/flood
simulations; provider reconciliation and payout monitoring; backend and
transport adapters; and measured Mini App/analytics integrations where
needed.

**Depends on:** M3–M5. **Acceptance:** documented SLO/error behavior, load
and recovery evidence, bounded resource use, operational runbooks and
compatibility review for every backend/transport. **Deferred:** features
without a measured use case, including AI moderation unless its lower authority
and cost limits are demonstrated.

## Optional application track — `imtg-admin`

The future `imtg-admin` operator console is a separate, optional application,
not a core-library milestone. Its UI may use [ImGuiX](https://github.com/LimiNode/ImGuiX),
while [mgc-platform](https://github.com/LimiNode/mgc-platform) is a reference
for GUI-shell lifecycle and model/controller organization. Neither repository
is a dependency of `imtg_core`, and no GUI source or transport is authorized
by this roadmap entry.

The primary integration path is an authenticated, versioned operator API to a
remote runtime. Local bot-process supervision is an optional deployment mode.
The console owns presentation and bounded local projections; runtime, commerce,
storage and authorization remain backend authorities. Bot, shop and operator
context must fence requests, drafts and late responses.

Implementation is gated on the [operator console contract](contracts/OPERATOR_CONSOLE.md),
an accepted operator API/DTO policy, authorization and audit conformance,
context-isolation and reconnect tests, and an explicitly approved separate GUI
target. The architecture decision and rejected alternatives are recorded in
[ADR 0008](adr/0008-operator-admin-console.md). Until those gates are met,
`imtg-admin` remains documentation-only and headless deployments remain
unchanged.

## Reference applications and ordering

These names describe separately buildable integration targets, not current
repositories or mandatory core features:

- `intake-bot` — multi-step applications, approvals and support tickets;
- `shop-bot` — catalog, verified payment, unique-item reservation and digital
  fulfillment;
- `guardian-bot` — moderation, bounded anti-flood and anti-raid recovery.

The dependency-aware order is: complete M2c-b/M2c-c gates; implement the safe
gateway, headless routing and permissions; freeze the provider-neutral payment
contract and capability audit; build one first end-to-end bot with one
eligible gateway or explicitly contracted commerce platform; add
forms/tickets, payment/fulfillment and moderation policy; add durable jobs and
the three reference applications; then add operations and advanced
integrations. Research interest is not permission to add speculative core code.
See [TELEGRAM_APPLICATION_PROFILES.md](research/TELEGRAM_APPLICATION_PROFILES.md)
for acceptance scenarios, [PAYMENTS.md](contracts/PAYMENTS.md) for payment
invariants, and [ADR 0006](adr/0006-headless-routing-and-application-composition.md)
for the composition decision.
