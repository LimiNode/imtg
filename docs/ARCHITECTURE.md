# imtg architecture

**Status:** contract-only design. **Semantic owner:** core architecture maintainers.

## Purpose and boundaries

imtg coordinates Telegram bot application state, deterministic Immediate-Mode views, typed actions/events, and durable effects. Core owns identities, scheduling contracts, UI tree semantics, controller boundaries, and storage/delivery interfaces. It does not own HTTP, JSON, Telegram credentials, a database engine, or application-specific business rules.

The runtime is organized as:

1. an `UpdateGateway` validates and normalizes external input;
2. an `ActionRouter` validates callback tokens, identity, revision policy, and authorization;
3. a per-`ConversationKey` scheduler serializes one logical session;
4. a controller reads model/session state and builds a UI tree;
5. callbacks dispatch typed events or short-lived commands;
6. application services commit state and outbox effects through a unit of work;
7. a renderer/reconciler compares desired and applied UI revisions and plans effects;
8. a `DeliveryWorker` performs Telegram calls with retry, rate limits, and explicit uncertainty.

Controllers may be concrete classes, free functions, or composition; inheritance is not required for small applications. `ApplicationModel` is application-owned state, while session/FSM records are runtime-owned persistence. `ViewContext` is read/render scoped; `ActionContext` is action scoped and must not outlive dispatch.

## Ownership and data flow

The UI tree owns immutable render data and stable component IDs for one render. The renderer owns conversion to a transport-neutral draft. The reconciler owns desired/applied revisions and idempotent decisions. UpdateGateway/ActionRouter own inbound validation and callback authorization. Delivery owns Telegram request attempts and uncertainty; it never invokes arbitrary callbacks from storage. Storage owns durable records, revisions, migrations, and transaction boundaries. Outbox records bridge committed state to external effects.

Events are domain messages, not Telegram requests. A handler may enqueue an outbox effect, but delivery remains a separate concern. C++ callbacks, pointers, references, and closures are never serialized.

## Concurrency and shutdown

Updates for the same logical `ConversationKey` are processed in order. Different keys may run concurrently if their storage and services are safe. Shutdown stops intake, drains or cancels bounded work according to policy, commits recoverable state, and leaves unsent effects in the outbox. No new callback may start after its action/subscription scope closes; already-running callbacks are allowed to finish only while their owner remains alive and the lifetime guard is valid.

See the normative contracts for [UI/actions](contracts/UI_AND_ACTIONS.md), [events/lifetime](contracts/EVENTS_AND_LIFETIME.md), [sessions/FSM](contracts/SESSIONS_AND_FSM.md), [storage](contracts/STORAGE.md), [delivery](contracts/TELEGRAM_DELIVERY.md), [localization](contracts/LOCALIZATION.md), and [users/history](contracts/USERS_CHATS_AND_HISTORY.md).

## Verification

Architecture is verified by contract tests and deterministic fakes: ordering per key, stable UI IDs, callback lifetime, transaction revision checks, outbox recovery, reconciliation idempotence, and shutdown behavior. A milestone cannot claim implementation from documentation alone.
