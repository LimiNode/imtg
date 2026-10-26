# imtg architecture

**Status:** contract-only design. **Semantic owner:** core architecture maintainers.

## Purpose and boundaries

imtg coordinates Telegram bot application state, deterministic Immediate-Mode views, typed actions/events, and durable effects. Core owns identities, scheduling contracts, UI tree semantics, controller boundaries, and storage/delivery interfaces. It does not own HTTP, JSON, Telegram credentials, a database engine, or application-specific business rules.

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
