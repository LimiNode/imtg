# imtg architecture

**Status:** contract-only design. **Semantic owner:** core architecture maintainers.

## Purpose and boundaries

imtg coordinates Telegram bot application state, deterministic Immediate-Mode views, typed actions/events, and durable effects. Core owns identities, scheduling contracts, UI tree semantics, controller boundaries, and storage/delivery interfaces. It does not own HTTP, JSON, Telegram credentials, a database engine, or application-specific business rules.

## Header topology and dependency direction

The public C++ surface follows a hybrid profile: domain umbrellas organize
the main library, while public components and optional modules have independent,
self-contained entry points. Optional modules own separate build targets;
core umbrellas never include optional adapters or vendor SDKs. A
complete domain owns a root-level entry point such as `<imtg/core.hpp>` and
keeps its leaves under `src/imtg/<domain>/`. Domain dependencies form a DAG;
umbrellas include lower-level public entry points before their own components,
while leaves never include their own umbrella. Cross-domain includes use
`<imtg/...>` entry points, intra-domain includes use local paths, and no `..`
relative include is permitted. Public standalone headers are self-contained;
private leaves are excluded from installation and may rely on a documented
umbrella context.

The audited current graph is `actions → core`, `storage → core`,
`sessions → storage`, `reconciliation → sessions`, `ui → actions`, and
`runtime → ui` (with direct core dependencies where required). The graph is
acyclic, but the current `core/core.hpp`, `actions/actions.hpp`,
`ui/ui.hpp`, and `runtime/runtime.hpp` locations are transitional. Storage,
sessions, and reconciliation do not yet have umbrellas. The staged migration,
install boundary, and compatibility policy are defined in
[header-architecture.md](guides/header-architecture.md) and
[ADR 0009](adr/0009-domain-header-architecture.md), refined by
[ADR 0011](adr/0011-hybrid-public-header-profile.md).

The Actions implementation snapshot `5dca497` now provides the root
`<imtg/actions.hpp>` entry point and responsibility-based standalone headers;
the old nested Actions paths remain compatibility shims during migration.

This documentation decision does not move headers or change runtime behavior.
Each later implementation PR must update the explicit install manifest,
standalone/umbrella tests, and any declared compatibility mapping together.

The runtime is organized as:

1. an `UpdateGateway` performs stateless validation, normalizes external input, validates token syntax/integrity, and derives the `ConversationKey`;
2. a per-`ConversationKey` scheduler serializes one logical session;
3. inside that boundary, state is loaded and `ActionRouter` validates current revision, identity, authorization, and action payload;
4. a controller handles the command through `ctx.dispatch()` and computes the FSM/state change;
5. transaction 1 atomically commits the processed update, state transition, and durable render intent;
6. a post-commit renderer/reconciler consumes that intent, loads committed state, compares desired and applied UI revisions, and plans delivery;
7. transaction 2 atomically writes the planned delivery effect to the outbox and marks/supersedes the render intent;
8. a `DeliveryWorker` performs Telegram calls with retry, rate limits, fencing, and explicit uncertainty, then persists the outcome.

Controllers may be concrete classes, free functions, or composition; inheritance is not required for small applications. `ApplicationModel` is application-owned state, while session/FSM records are runtime-owned persistence. `ViewContext` is read/render scoped; `ActionContext` is action scoped and must not outlive dispatch.

## Ownership and data flow

The UI tree owns immutable render data and stable component IDs for one render. The renderer owns conversion to a transport-neutral draft. The reconciler owns desired/applied revisions and idempotent decisions. `UpdateGateway` owns only stateless inbound checks before scheduling; state-dependent authorization belongs to `ActionRouter` inside the serialized boundary and is repeated against the current storage revision before commit. Delivery owns Telegram request attempts and uncertainty; it never invokes arbitrary callbacks from storage. Storage owns durable records, revisions, render intents, migrations, and transaction boundaries. Outbox records bridge reconciled intent to external effects.

`ctx.dispatch()` is a synchronous transactional command path: successful update processing cannot be recorded until its required state changes and durable render intent commit. EventBus publication is a separate notification mechanism; an asynchronous or non-persistent event may not be the sole carrier of a required transition or effect. Events are domain messages, not Telegram requests. C++ callbacks, pointers, references, and closures are never serialized.

## Concurrency and shutdown

Updates for the same logical `ConversationKey` are processed in order. Different keys may run concurrently if their storage and services are safe. Shutdown stops intake, drains or cancels bounded work according to policy, commits recoverable state, and leaves unsent effects in the outbox. No new callback may start after its action/subscription scope closes; already-running callbacks are allowed to finish only while their owner remains alive and the lifetime guard is valid.

See the normative contracts for [UI/actions](contracts/UI_AND_ACTIONS.md), [events/lifetime](contracts/EVENTS_AND_LIFETIME.md), [sessions/FSM](contracts/SESSIONS_AND_FSM.md), [storage](contracts/STORAGE.md), [delivery](contracts/TELEGRAM_DELIVERY.md), [localization](contracts/LOCALIZATION.md), and [users/history](contracts/USERS_CHATS_AND_HISTORY.md).

## Verification

Architecture is verified by contract tests and deterministic fakes: ordering per key, two concurrent callbacks against one revision, stable UI IDs, callback lifetime, transaction revision checks, render-intent recovery, fenced outbox acknowledgment, reconciliation idempotence, and shutdown behavior. A milestone cannot claim implementation from documentation alone.
