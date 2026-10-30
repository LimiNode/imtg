# M2 implementation plan

M2 starts only after D0 and the corrected M1 stack are independently approved and merged in order. M2a introduces the backend-neutral protocol and deterministic in-memory semantic baseline; durable repositories, delivery claims, and MDBX remain deferred to later M2 slices.

## M2a — backend-neutral storage protocol

Define `IStorageBackend`, `ITransaction`, `UnitOfWork`, typed storage errors, and `CommitOutcome` (`Committed`, `RejectedConflict`, `Failed`, `Unknown`). Transactions are thread-bound, own rollback state, expose read-your-writes, and validate expected revisions/CAS at commit. Schema metadata and migration records are part of the protocol. The in-memory backend is the reference implementation and supplies a reusable conformance suite.

Implemented in `imtg::core`: `IStorageBackend`, `ITransaction`, move-only `UnitOfWork`, typed storage errors, schema gates, expected-revision CAS, stable snapshots, read-your-writes, monotonic tombstone generations, and atomic in-memory publication. The fixture-based conformance executable covers read-your-writes, rollback/ownership, stable snapshots, duplicate create, repeated writes with one committed increment, delete/recreate ABA rejection, unknown commit recovery, thread ownership, read-only rejection, schema intake, and atomic multi-record transitions. Fixtures declare whether multiple active writers are supported so a future single-writer MDBX backend can reuse the portable cases. The in-memory backend is a semantic baseline, not a durability guarantee.

## M2b — typed sessions, FSM, and update deduplication

Implemented on the in-memory semantic baseline: typed, schema-versioned session records with bounded
binary-safe encoding; guarded deterministic FSM transitions; pending-input/deadline fields; cancellation,
timeout, and terminal-state handling; optimistic session revisions; processed-update deduplication; and
minimal stable render intents. `AtomicUpdateProcessor` receives one `UnitOfWork` and commits the
processed marker, session transition, and optional render intent atomically. Repository reconstruction
against the same backend proves backend-lifetime recovery only; physical process-crash durability and
explicit migration execution remain deferred to the durable adapter.

## M2c — render-intent processing and durable outbox

The proposed protocol and failure-injection matrix are defined in
[ADR 0005](adr/0005-m2c-render-intent-reconciliation.md). This is a design
gate only: no claim/outbox implementation or process-crash durability is
declared by this document.

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
