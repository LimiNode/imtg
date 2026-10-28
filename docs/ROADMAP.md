# Roadmap

Milestones are dependency-gated; independent research may proceed in parallel, but acceptance evidence is required before declaring a milestone complete.

## Dependency map

```text
                         +--> M4 (localization/services) --+
D0 --> M1 --> M2 -------+                                  +--> M5 --> M6
  \       \             +--> M3 (delivery) ---------------+
   \       +--> locale contract/tests may proceed after M1
    +--> governance/contracts (gate all implementation)
```

The graph shows implementation blockers, not a mandatory serial work queue. M4 contract/test prototyping may proceed after M1, while durable M4 services depend on M2. M3 depends on M2 for outbox/session revisions. M5 depends on storage and delivery contracts but does not block M4 localization work.

## D0 — Architecture & Engineering Foundation

**Scope:** contracts, invariants, governance, ADR, scenarios, roadmap, and honest status. **Dependencies:** none. **Deliverables:** this documentation layer. **Acceptance:** links and terminology are consistent; examples are marked conceptual; `git diff --check` passes. **Tests:** documentation review and link/status audit. **Exit:** a new agent can identify ownership, limits, and next milestone. **D0 restrictions:** do not add production code, CMake targets, dependencies, submodules, or adapters in this milestone; keep the PR documentation-first and do not merge it without explicit approval. **Deferred:** implementation and CI.

## M1 — Typed UI tree, callbacks, controllers, deterministic core

M1 is delivered as independently reviewable stacked PRs: **M1a** foundation and typed identities, **M1b** deterministic UI tree, **M1c** action registry/callback semantics, and **M1d** scheduler/reference runtime. Each PR must build and test against its declared base; the M1 milestone remains `partial` until all four acceptance gates pass.

M1b specifically freezes the transport-neutral node vocabulary (text, heading, section, separator, row, and button), immutable render snapshots, explicit stable paths, duplicate detection, and move-only RAII grouping guards. Telegram formatting and transport concerns remain deferred.

M1c specifically provides application-level versioned routes, typed payload codecs, `ActionHandle` binding, scoped `ActionContext`, restart reconstruction, and an injectable token codec. The initial plain codec is deliberately non-authenticating; Telegram token authentication remains an adapter concern for M3.

M1d specifically provides a bounded worker pool with per-key sequential mailboxes, bounded queue rejection, synchronous command execution, fake desired/applied reconciliation, callback exception reporting, observable fail-stop handling for worker requeue failure, controlled shutdown, and the local dispatcher boundary described in ADR 0002. It does not provide durable storage, production outbox, or Telegram delivery.

**Scope:** Telegram-free render tree, stable IDs, `snake_case` UI API, versioned action routes, serializable payload validation, action scope, typed events, controller/model composition, deterministic scheduler and reconciler fakes. **Depends on:** D0; M1 must use the accepted `ConversationKey` (`bot_id + chat_id + Chat` or `UserInChat`). **Architectural decisions closed before implementation:** action-token lifecycle, route reconstruction after restart, callback lifetime, key equality/encoding, and per-key ordering. **May remain open:** thread/topic transport mapping, reconciler conflict policy, dispatcher implementation, and RAII grouping syntax. **Deliverables:** public headers, tests, API docs, examples. **Acceptance:** repeated renders are deterministic; malformed/unknown/stale callbacks are rejected; callbacks cannot escape scope; route handlers resolve after restart; per-key ordering is tested; reconciliation is idempotent. **Tests:** unit, property/fuzz tests for IDs/tokens, deterministic integration tests. **Exit:** M2 can persist session contracts without changing UI semantics. **Deferred:** Telegram delivery and database adapters.

## M2 — FSM, sessions, storage contracts, first MDBX backend

**M2a status:** implemented. `IStorageBackend`, `ITransaction`, `UnitOfWork`, fail-closed schema metadata, monotonic expected revisions with delete/recreate ABA protection, deterministic commit outcomes, and the in-memory snapshot/optimistic backend are available in `imtg::core`; the fixture-based reusable conformance suite passes for that backend. The implementation is not durable and does not provide sessions, render intents, outbox claims, or MDBX.

**Remaining M2 scope:** typed states/events, guarded transitions, recovery, durable render intents, fenced outbox claims, migrations, and the MDBX adapter. **Depends on:** M1 and MDBX integration review. **Acceptance:** the reference backend semantics are retained while the conformance suite passes for in-memory and MDBX; crash recovery between command commit and outbox planning is tested; uncertain commit, revision conflict, and stale acknowledgment behavior are tested; no per-user DBI design. **Exit:** durable state and render intent can be committed atomically, then converted recoverably to an outbox effect. **Deferred:** Telegram calls and cross-database atomicity.

## M3 — Telegram delivery and outbox

**Scope:** kurlyk-backed adapter, polling, callback token validation, desired/applied revisions, retries, rate limiting, uncertain outcomes, and outbox workers. **Depends on:** M2 and verified Telegram API behavior. **Acceptance:** fake transport tests cover send/edit/retry/429/unknown outcomes; external delivery is explicitly at-least-once or best-effort; shutdown leaves recoverable outbox work. **Exit:** a minimal bot can run against Telegram with documented operational limits. **Deferred:** webhooks and rich UI.

## M4 — Localization and core domain services

**Scope:** immutable JSON catalogs, fallback, plural rules, templates, users, chats, settings, and locale-aware rendering. **Depends on:** M1; durable services depend on M2. **Acceptance:** concurrent users can render different locales; IDs do not change with translation; missing keys and plural categories have deterministic fallbacks. **Exit:** reference menu, settings, and registration scenarios have contract tests. **Deferred:** message history retention and advanced access control.

## M5 — SQLite, history, access control, jobs

**Scope:** SQLite adapter, optional message history, roles/permissions/bans, persistent jobs, and support/approval scenarios. **Depends on:** M2–M4. **Acceptance:** storage conformance and retention/deletion tests pass; job retries are bounded and observable; access decisions are auditable. **Exit:** reference scenarios 3–5 run on fakes and at least one durable backend. **Deferred:** advanced integrations.

## M6 — Webhooks, rich UI, operations, extensions

**Scope:** webhook adapter, richer components, operational telemetry, backend extensions, and advanced integrations. **Depends on:** M3–M5. **Acceptance:** documented SLO/error behavior, load and recovery evidence, and compatibility review for each backend/transport. **Deferred:** features without a measured use case.
