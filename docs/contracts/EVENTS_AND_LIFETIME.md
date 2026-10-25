# Events and lifetime contract

**Semantic owner:** runtime maintainers. **Status:** contract-only.

Typed events carry value-owned DTOs or explicitly immutable shared payloads. Synchronous delivery is bounded to the current dispatch scope; deferred delivery is queued with a size, cancellation, and shutdown policy. Subscriptions are RAII objects; destroying a subscription prevents future delivery. Handlers must not retain borrowed contexts or references beyond the documented call.

Exceptions are isolated at the dispatcher boundary and reported to the technical journal. Reentrancy is controlled with a queue/depth policy to prevent unbounded event chains. Graceful shutdown stops intake, drains permitted work, cancels the rest, and preserves durable effects in the outbox.

**Verification:** ordering and reentrancy tests, subscription lifetime tests, exception isolation, bounded queue tests, and shutdown/restart tests. **Dependencies:** storage/outbox and per-session scheduler.
