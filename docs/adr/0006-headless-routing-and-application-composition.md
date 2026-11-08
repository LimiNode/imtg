# ADR 0006: headless routing and application composition

**Status:** Proposed; implementation begins in M3 after independent review.

## Context

The M1/M2 contracts center interactive work on a `ConversationKey`, session/FSM
state and optional render intents. A moderation bot must process a message,
join request or flood signal without creating a screen. A shop must answer a
payment pre-checkout update within the Telegram deadline even when a UI session
is idle or its render queue is busy. The Telegram Bot API currently requires a
`pre_checkout_query` response within ten seconds after Telegram sends the
query
([official method documentation](https://core.telegram.org/bots/api#answerprecheckoutquery));
the bot does not receive that upstream send timestamp. A support adapter must
therefore treat the local receipt timestamp as a monotonic accounting point,
not as the start of the remote window. It uses a conservative configured
admission budget that includes delivery uncertainty, queueing, validation,
handler execution, HTTP round-trip and a safety margin; it never promises that
meeting this local budget guarantees Telegram acceptance. Chat-join requests
are a separate urgent class and must not inherit the pre-checkout deadline.
A support bot needs both dialog forms and background notifications.

Independent event frameworks would duplicate imtg's keyed scheduling, shutdown
and transaction boundaries. Making every update a UI render would couple
domain state to presentation and create unnecessary outbox work.

## Decision

M3 introduces one typed `UpdateRouter` at the Telegram adapter boundary. It
supports declarative filters, middleware, an imtg-owned deadline policy and
explicit outcomes (`Accepted`, `Rejected`, `QueueFull`, `DeadlineMissed`,
`Closed`, `Failed`). It submits work to the existing scheduler and bounded
admission controls; this is not a second generic dispatcher.

Handlers declare one of two modes:

- **interactive:** may load session/FSM state and publish a render intent;
- **headless:** runs a domain command or plans a domain effect without a UI
  screen, UI tree or render intent.

Urgent update classes such as payment pre-checkout and join requests use a
bounded urgent admission lane inside that scheduler. The lane has a finite
capacity and a finite worker/burst budget, with a reserved worker slot (or
equivalent reserved execution budget) so long ordinary handlers cannot occupy
all urgent capacity. It shares the normal worker pool's shutdown, quiescence
and failure accounting. A configured finite urgent-burst limit and minimum
ordinary service share provide weighted fairness, so ordinary work cannot
starve. It is a scheduling policy, not a permission shortcut: filters,
middleware, authorization and payment-state CAS still run before an urgent
response is accepted.

Each admitted item carries a receipt time, an explicit effective local
deadline budget and a bounded handler-cost budget. The scheduler rejects an
item as `DeadlineMissed` when its remaining local budget, including the
safety margin, cannot cover that declared cost; overload returns `QueueFull`.
Both outcomes are observable and fail closed. In particular, a payment
pre-checkout path never acknowledges a payment as valid unless its required
verification and transactional CAS have completed.

Payment lifecycle is deliberately split. Pre-checkout validates the order,
amount, currency, availability and authorization, and an affirmative answer
only permits the provider to continue; it is not proof of `Paid`. A later
`successful_payment` update is the authoritative, deduplicated payment event.
Only after that event commits may fulfillment reserve inventory or deliver a
digital item. A lost or ambiguous external response remains explicitly
uncertain and never authorizes fulfillment by itself.

The normal and urgent lanes share a domain-conflict gate. Updates that can
touch the same payment/order or other domain key serialize through the same
keyed turn and expected-revision/CAS check, so cross-lane execution cannot
silently reorder conflicting state. An urgent item for an unrelated key may
run while a long interactive handler occupies another key. An urgent item
that conflicts with a currently running handler waits only within that key;
if its deadline expires it returns `DeadlineMissed` rather than bypassing the
handler or fabricating success. Background jobs use the planned durable job
contract, not an unbounded callback queue.

The existing M2c-b `OutboxEffect` and logical-slot identity are preserved for
interaction effects. Moderation and commerce state changes are local
transactional domain writes. A module may own a typed domain outbox or command
adapter for its external effects; no speculative `EffectKind` values are added
to core. Every external category defines its own idempotency, retry and
`UnknownOutcome` proof.

Payment-provider identities, capability checks and webhook/reconciliation
invariants are owned by the provider-neutral payment contract in
[`PAYMENTS.md`](../contracts/PAYMENTS.md); this routing ADR does not choose a
vendor or make commerce adapters part of the Telegram gateway.

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
- module composition and dependency injection are additional M4 contracts;
- a deadline is a bounded response budget, not a guarantee that Telegram will
  accept a late response; transport and provider limits remain observable.

## Rejected alternatives

1. **A second generic dispatcher:** duplicates event-hub/runtime ownership and
   makes ordering, shutdown and overload behavior ambiguous.
2. **RenderIntent for every update:** wastes storage/effect capacity and forces
   moderation/payment domains into UI state.
3. **One universal commerce/moderation effect enum:** conflates local state
   transitions with external Telegram calls and weakens M2c-b migration and
   recovery identity.

## Gates

Before implementation is called complete, deterministic tests must show:

1. a long-running interactive handler on key A does not block an unrelated
   urgent pre-checkout on key B;
2. an urgent item that conflicts with an active handler on the same domain key
   is serialized and becomes `DeadlineMissed` if its budget expires;
3. urgent and ordinary capacities are bounded, overload returns `QueueFull`,
   and weighted fairness lets ordinary work continue;
4. concurrent payment updates are authorized and resolved by the same
   revision/CAS contract, with no valid acknowledgement before verification;
5. shutdown/quiescence accounts for both lanes and drains or reports every
   admitted item;
6. filters/middleware are deterministic, headless handling creates no render
   intent, permission checks are enforced, restart/recovery is covered, and a
   lost external response produces explicit `UnknownOutcome`.
