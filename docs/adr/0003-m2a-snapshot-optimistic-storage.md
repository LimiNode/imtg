# ADR 0003: M2a snapshot/optimistic storage baseline

## Status

Accepted for M2a; durable adapters remain future work.

## Decision

The backend-neutral storage protocol uses a stable transaction snapshot, read-your-writes, and
optimistic expected-revision checks at commit. A transaction is owned by one creating thread.
`UnitOfWork` is move-only and rolls back only a definitely active transaction during destruction.
Move-assignment applies the same rule before replacing an owned transaction. All pending mutations
publish atomically as one write set.

Each changed key advances exactly one non-zero generation per successful transaction. Repeated
writes use the transaction-visible candidate generation but retain the original snapshot generation
for commit validation. Deletes publish tombstones instead of discarding generation history;
recreation advances from the tombstone and therefore rejects stale pre-delete writers. Tombstones
remain for the in-memory backend lifetime. Durable adapters may reclaim them only with an
equivalent generation registry or a proven safe epoch policy.

Commit acknowledgement is explicit: `Committed`, `RejectedConflict`, `Failed`, or `Unknown`.
`Unknown` is terminal and may have published data; callers recover by reading authoritative state
in a new transaction and never replay or roll back the uncertain operation.

The in-memory backend is deterministic and intentionally non-durable. It is the semantic baseline
for the reusable conformance suite, not a production persistence implementation. It permits
multiple active optimistic writers. Conformance fixtures declare this capability so a future MDBX
fixture with a single native writer is not required to emulate unsupported writer concurrency.

Only schema metadata in the `Stable` state accepts transaction intake. Applications still own the
startup call to `validate_schema()`; `Migrating` and `RecoveryRequired` fail closed.

## Consequences

The protocol can be tested without MDBX and keeps backend-specific types out of `imtg::core`.
Revision zero remains an internal no-generation sentinel, and overflow fails with a typed error
instead of wrapping. Durable migration execution/recovery, leases/fencing, and repository models
are deferred to later M2 slices.
