# Roadmap

Milestones are dependency-gated; independent research may proceed in parallel, but acceptance evidence is required before declaring a milestone complete.

## Dependency map

```text
D0 -> M1 -> M2 -> M3 -> M4 -> M5 -> M6
              \-> (M4 localization work may prototype in parallel after M1 contracts)
```

## D0 — Architecture & Engineering Foundation

**Scope:** contracts, invariants, governance, ADR, scenarios, roadmap, and honest status. **Dependencies:** none. **Deliverables:** this documentation layer. **Acceptance:** links and terminology are consistent; examples are marked conceptual; `git diff --check` passes. **Tests:** documentation review and link/status audit. **Exit:** a new agent can identify ownership, limits, and next milestone. **Deferred:** production code, dependencies, CI, and adapters.

## M1 — Typed UI tree, callbacks, controllers, deterministic core

**Scope:** Telegram-free render tree, stable IDs, `snake_case` UI API, action scope, typed events, controller/model composition, deterministic scheduler and reconciler fakes. **Depends on:** D0. **Deliverables:** public headers, tests, API docs, examples. **Acceptance:** repeated renders are deterministic; malformed/unknown callbacks are rejected; callbacks cannot escape scope; per-key ordering is tested; reconciliation is idempotent. **Tests:** unit, property/fuzz tests for IDs and callback tokens, deterministic integration tests. **Exit:** M2 can persist session contracts without changing UI semantics. **Deferred:** Telegram delivery and database adapters.

## M2 — FSM, sessions, storage contracts, first MDBX backend

**Scope:** typed states/events, guarded transitions, recovery, `IStorageBackend`, `ITransaction`, `UnitOfWork`, expected revisions, migrations, and MDBX adapter. **Depends on:** M1 and MDBX integration review. **Acceptance:** conformance suite passes for in-memory and MDBX; crash/recovery and conflict behavior are documented and tested; no per-user DBI design. **Exit:** durable runtime state and outbox records can be committed transactionally within one backend. **Deferred:** Telegram calls and cross-database atomicity.

## M3 — Telegram delivery and outbox

**Scope:** kurlyk-backed adapter, polling, callback token validation, desired/applied revisions, retries, rate limiting, uncertain outcomes, and outbox workers. **Depends on:** M2 and verified Telegram API behavior. **Acceptance:** fake transport tests cover send/edit/retry/429/unknown outcomes; external delivery is explicitly at-least-once or best-effort; shutdown leaves recoverable outbox work. **Exit:** a minimal bot can run against Telegram with documented operational limits. **Deferred:** webhooks and rich UI.

## M4 — Localization and core domain services

**Scope:** immutable JSON catalogs, fallback, plural rules, templates, users, chats, settings, and locale-aware rendering. **Depends on:** M1; durable services depend on M2. **Acceptance:** concurrent users can render different locales; IDs do not change with translation; missing keys and plural categories have deterministic fallbacks. **Exit:** reference menu, settings, and registration scenarios have contract tests. **Deferred:** message history retention and advanced access control.

## M5 — SQLite, history, access control, jobs

**Scope:** SQLite adapter, optional message history, roles/permissions/bans, persistent jobs, and support/approval scenarios. **Depends on:** M2–M4. **Acceptance:** storage conformance and retention/deletion tests pass; job retries are bounded and observable; access decisions are auditable. **Exit:** reference scenarios 3–5 run on fakes and at least one durable backend. **Deferred:** advanced integrations.

## M6 — Webhooks, rich UI, operations, extensions

**Scope:** webhook adapter, richer components, operational telemetry, backend extensions, and advanced integrations. **Depends on:** M3–M5. **Acceptance:** documented SLO/error behavior, load and recovery evidence, and compatibility review for each backend/transport. **Deferred:** features without a measured use case.
