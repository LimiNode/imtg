# M2 implementation plan

M2 starts only after D0 and the corrected M1 stack are independently approved and merged in order. M2a introduces the backend-neutral protocol and deterministic in-memory semantic baseline; durable repositories, delivery claims, and MDBX remain deferred to later M2 slices.

## M2a — backend-neutral storage protocol

Define `IStorageBackend`, `ITransaction`, `UnitOfWork`, typed storage errors, and `CommitOutcome` (`Committed`, `RejectedConflict`, `Failed`, `Unknown`). Transactions are thread-bound, own rollback state, expose read-your-writes, and validate expected revisions/CAS at commit. Schema metadata and migration records are part of the protocol. The in-memory backend is the reference implementation and supplies a reusable conformance suite.

Implemented in `imtg::core`: `IStorageBackend`, `ITransaction`, move-only `UnitOfWork`, typed storage errors, schema metadata, expected-revision CAS, stable snapshots, read-your-writes, and atomic in-memory publication. The reusable conformance executable covers read-your-writes, rollback, conflicts, duplicate create, repeated writes, unknown commit recovery, transaction ownership, read-only rejection, schema validation, and atomic multi-record transitions. The in-memory backend is a semantic baseline, not a durability guarantee.

## M2b — durable sessions and FSM

Persist typed session records with schema/version tags, guarded transitions, pending input, timeout/cancellation transitions, processed-update deduplication, optimistic revisions, restart recovery, and an explicit migration policy. Runtime command execution receives one `UnitOfWork`; session transition, deduplication, and render-intent creation commit atomically.

## M2c — render intents and durable outbox

Use the two-transaction protocol:

```text
TX1: ProcessedUpdate + Session/FSM transition + RenderIntent -> Commit
post-commit: render desired UI and reconcile
TX2: OutboxEffect + Complete/Supersede RenderIntent -> Commit
delivery: claim effect, execute external operation, record fenced outcome
```

Records have stable identities. Claims use leases and monotonically increasing fencing tokens. Recovery defines retry eligibility, supersession, `UnknownOutcome`, expected-revision checks, bounded pagination, and crash behavior between every phase. A failure-injection matrix is required before implementation.

## M2d — MDBX adapter

Use `mdbx-containers` behind an optional CMake integration. Keep MDBX types out of public core headers. Define connection ownership, thread-bound transactions, schema metadata, sortable composite keys, snapshot semantics, migration/recovery, and explicit DBI budgeting. The minimal runtime profile is `meta`, `sessions`, `processed_updates`, `render_intents`, `outbox`, and `event_journal`; never create a DBI per user or chat.

## M2e — conformance and recovery integration

Run the same backend-neutral suite against InMemory and MDBX: schema compatibility, atomic multi-record commit, duplicate updates, revision conflicts, render-intent recovery, TX1/TX2 crash points, stale fencing rejection, uncertain commit, interrupted migration restart, bounded scans, and shutdown with pending work.

## Entry gates

- M1 exact-head CI is green in Debug and Release; ASan/UBSan is green where supported.
- Action registration generations, dispatcher lifetime/reentrancy, bounded admission, and runtime shutdown contracts are approved.
- The failure-injection matrix and transaction ownership rules are reviewed before any MDBX code is added.
