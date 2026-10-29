# ADR 0004: M2b session/FSM transaction boundary

## Status

Accepted for M2b; durable adapters and delivery workers remain future work.

## Decision

Sessions use a versioned, bounded, length-prefixed binary-safe record format keyed by the canonical
`ConversationKey` encoding. The storage revision returned by `ITransaction` is authoritative; the
revision copied into a serialized value is diagnostic and is refreshed on repository load. Session
repositories borrow a caller-owned transaction and never commit independently.

`Fsm` is a pure deterministic transition graph. State and event identities are versioned; guards are
process-local functions and are never serialized. Cancellation and timeout are typed events, terminal
states reject further transitions, and an explicit deadline causes the processor to synthesize a timeout
event before evaluating a normal event. The `cancel/1` and `timeout/1` identities are reserved for
their corresponding event kinds: normal events cannot use either identity, and system kinds cannot use
ordinary identities. On a successful state change the bounded default session delta clears pending input
and the old deadline and sets the screen to the new state; processor options may replace these fields.
Render policy is explicit (`Never`, `OnChange`, or `Always`), with `Always` intent IDs incorporating the
originating update. Update identity must match the conversation bot before deduplication. Applications
may require an expected session revision at the processor boundary; authorization and action binding
remain outside M2b.

`AtomicUpdateProcessor` stages `record_if_new(update_identity)`, the session create/transition, and an
optional stable `RenderIntent` in one optimistic transaction. A duplicate marker commits as a no-op and
does not execute a second transition. A failed or conflicted transaction publishes none of the staged
records; `Unknown` is terminal and requires authoritative inspection in a new transaction. Minimal
render intents stop at durable intent creation; claiming, reconciliation, outbox effects, and Telegram
calls are M2c/M3 concerns.

## Consequences

The semantic baseline can test duplicate suppression, stale revisions, malformed records, and atomicity
without MDBX or transport dependencies. Repository reconstruction proves only that state survives for
the lifetime of the in-memory backend; no process-crash durability or exactly-once guarantee beyond the
configured retention policy is claimed. Guards cannot capture persisted callbacks or pointers.
