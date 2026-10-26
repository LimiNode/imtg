# ADR 0002: Local dispatcher boundary for the M1 reference runtime

- **Status:** accepted for M1d reference runtime
- **Date:** 2026-10-25
- **Owners:** imtg maintainers

## Decision

M1d uses a small `NotificationDispatcher` implementation behind an explicit public boundary. It has RAII subscription handles, bounded runtime queues, callback isolation, injectable callback-error reporting, bounded nested publication, and `close_and_wait()` quiescence. Removing a subscription prevents future invocation; callbacks selected by an already-started publication may finish even after close. `close_and_wait()` rejects self-wait from callback/error-sink execution, waits for both subscriber callbacks and all active publication scopes (including error reporting), and does not invoke user handlers under internal locks. Owners wait for quiescence before destruction. `ReferenceRuntime` uses a bounded worker pool with per-`ConversationKey` serial mailboxes, transactional admission rollback, global/per-key admission limits, and a synchronous command sink; notification publication is not the persistence path for required state changes.

The core does not include `event-hub-cpp` headers in M1. A later adapter may map this boundary to event-hub after a lifetime and performance review, without changing action/session contracts.

## Consequences

The reference runtime is deterministic, dependency-free, and testable offline. It intentionally does not provide production persistence, delivery, or cryptographic token validation. Worker shutdown requested from a callback signals the coordinator; `shutdown()` reports an explicit error when called from a worker, and an external coordinator/destructor completes the drain. A requeue allocation failure is fail-stop and observable through `RuntimeFailure` plus a discarded-task count; its worker recovery path stores only pre-existing status fields and does not allocate diagnostic strings. It never silently reports a normal shutdown.
