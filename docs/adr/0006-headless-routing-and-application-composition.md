# ADR 0006: headless routing and application composition

**Status:** accepted for roadmap planning; implementation begins in M3.

## Context

The M1/M2 contracts center interactive work on a `ConversationKey`, session/FSM
state and optional render intents. A moderation bot must process a message,
join request or flood signal without creating a screen. A shop must answer a
payment pre-checkout update within the Telegram deadline even when a UI session
is idle or its render queue is busy. A support bot needs both dialog forms and
background notifications.

Independent event frameworks would duplicate imtg's keyed scheduling, shutdown
and transaction boundaries. Making every update a UI render would couple
domain state to presentation and create unnecessary outbox work.

## Decision

M3 introduces one typed `UpdateRouter` at the Telegram adapter boundary. It
supports declarative filters, middleware, handler priority and explicit
outcomes (`Accepted`, `Rejected`, `QueueFull`, `Closed`, `Failed`). It submits
work to the existing per-key scheduler and bounded admission controls.

Handlers declare one of two modes:

- **interactive:** may load session/FSM state and publish a render intent;
- **headless:** runs a domain command or plans a domain effect without a UI
  screen, UI tree or render intent.

Urgent update classes such as payment pre-checkout and join requests may have a
separate priority/admission budget, but they remain visible to the same
shutdown and observability accounting. Background jobs use the planned durable
job contract, not an unbounded callback queue.

The existing M2c-b `OutboxEffect` and logical-slot identity are preserved for
interaction effects. Moderation and commerce state changes are local
transactional domain writes. A module may own a typed domain outbox or command
adapter for its external effects; no speculative `EffectKind` values are added
to core. Every external category defines its own idempotency, retry and
`UnknownOutcome` proof.

## Consequences

Positive:

- anti-spam and payment handlers do not pay for UI/session/render work;
- interactive applications retain the existing deterministic UI contracts;
- per-key ordering, bounded queues, fail-stop and shutdown semantics are reused;
- domain identities and retention policies remain independent from Telegram
  callback/message identities;
- a first real bot can be built before every optional domain module exists.

Costs and limits:

- the router must define update normalization, authorization and priority
  policy before transport implementation;
- urgent lanes need explicit budgets so they cannot starve ordinary traffic;
- Telegram permissions, privacy mode and payment deadlines remain adapter
  constraints, not guarantees of the framework;
- module composition and dependency injection are additional M4 contracts.

## Rejected alternatives

1. **A second generic dispatcher:** duplicates event-hub/runtime ownership and
   makes ordering, shutdown and overload behavior ambiguous.
2. **RenderIntent for every update:** wastes storage/effect capacity and forces
   moderation/payment domains into UI state.
3. **One universal commerce/moderation effect enum:** conflates local state
   transitions with external Telegram calls and weakens M2c-b migration and
   recovery identity.

## Gates

Before implementation is called complete, tests must show: deterministic
filter/middleware/priority order; headless handling with no render intent;
payment deadline handling; bounded overload under a message flood; permission
checks; per-key ordering; restart/recovery; and explicit UnknownOutcome for a
lost external response.
