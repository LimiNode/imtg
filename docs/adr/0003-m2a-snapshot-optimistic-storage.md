# ADR 0003: M2a snapshot/optimistic storage baseline

## Status

Accepted for M2a; durable adapters remain future work.

## Decision

The backend-neutral storage protocol uses a stable transaction snapshot, read-your-writes, and
optimistic expected-revision checks at commit. A transaction is owned by one creating thread.
`UnitOfWork` is move-only and rolls back only a definitely active transaction during destruction.
All pending mutations publish atomically as one write set.

Commit acknowledgement is explicit: `Committed`, `RejectedConflict`, `Failed`, or `Unknown`.
`Unknown` is terminal and may have published data; callers recover by reading authoritative state
in a new transaction and never replay or roll back the uncertain operation.

The in-memory backend is deterministic and intentionally non-durable. It is the semantic baseline
for the reusable conformance suite, not a production persistence implementation.

## Consequences

The protocol can be tested without MDBX and keeps backend-specific types out of `imtg::core`.
Repeated writes to one record retain the original compare-and-swap revision while exposing each
pending revision to the transaction. Durable migration recovery, leases/fencing, and repository
models are deferred to later M2 slices.
