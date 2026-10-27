# Storage contract

**Semantic owner:** persistence maintainers. **Status:** M2a protocol and deterministic in-memory reference backend are implemented in `src/imtg/storage/storage.hpp`; durable repositories, claims, render intents, outbox, and MDBX remain planned.

## Core interfaces

```cpp
// M2a API excerpt; the durable repository interfaces below remain planned.
enum class CommitOutcome { Committed, RejectedConflict, Failed, Unknown };
struct CommitResult {
    CommitOutcome outcome;
    std::optional<Error> error;
};

class ITransaction;

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;
    virtual Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) = 0;
};

class ITransaction {
public:
    virtual ~ITransaction() = default;
    virtual CommitResult commit() = 0;
    virtual void rollback() noexcept = 0;
};

class UnitOfWork {
public:
    static Result<UnitOfWork> begin(IStorageBackend&, TransactionMode);
    CommitResult commit();
};

class ISessionStore {
public:
    virtual ~ISessionStore() = default;
    virtual Result<SessionRecord> load(
        const ConversationKey&, ITransaction&) = 0;
    virtual Result<void> save(
        const SessionRecord&, Revision expected, ITransaction&) = 0;
};

class IRenderIntentStore {
public:
    virtual ~IRenderIntentStore() = default;
    virtual Result<RenderIntentId> append(RenderIntent, ITransaction&) = 0;
    virtual Result<std::vector<ClaimedRenderIntent>> claim_batch(
        ClaimRequest, ITransaction&) = 0;
    virtual Result<void> complete(
        RenderIntentId, ClaimToken fencing_token,
        Revision expected, ITransaction&) = 0;
};

class IOutboxStore {
public:
    virtual ~IOutboxStore() = default;
    virtual Result<OutboxId> append(OutboxEffect, ITransaction&) = 0;
    virtual Result<std::vector<ClaimedOutboxEffect>> claim_batch(
        ClaimRequest, ITransaction&) = 0;
    virtual Result<void> acknowledge(
        OutboxId, ClaimToken fencing_token,
        DeliveryOutcome, ITransaction&) = 0;
};

class IProcessedUpdateStore {
public:
    virtual ~IProcessedUpdateStore() = default;
    virtual Result<bool> record_if_new(
        UpdateIdentity, ITransaction&) = 0;
};
```

`UnitOfWork::begin()` returns a move-only owner of one transaction. The transaction borrows its
backend, so the backend must outlive the `UnitOfWork` and every repository call using it. A
transaction is owned by the thread that created it; cross-thread access is rejected. Its snapshot
is stable, reads include pending writes, and all writes in one commit are published atomically.
`create_if_absent`, `write`, and `erase` use expected revisions. Revision zero is reserved for a
key with no generation. Each changed key advances exactly once per successful transaction;
repeated writes expose the same candidate committed revision, and a fresh transaction observes
that exact revision after commit.
The destructor rolls back only a definitely active transaction. After `Unknown`, the transaction
is terminal: callers must recover by reading authoritative state in a new transaction and must not
retry or roll back the original operation.

Names and value layouts may change during implementation, but these semantics are normative. `CommitResult` always carries one outcome and may carry diagnostic error data: `Committed` is visible, `RejectedConflict` and `Failed` are known not committed, and `Unknown` may be committed. A transaction belongs to one worker/thread. Repository calls borrow it and never commit independently. `UnitOfWork` rolls back on destruction only while the transaction is definitely uncommitted. After `CommitOutcome::Unknown`, it enters an indeterminate terminal state: it must not call rollback, replay writes, or report failure as if no commit occurred. Recovery identifies the operation using durable update/intent IDs and reads authoritative storage state in a new transaction.

The in-memory reference backend uses snapshot/optimistic semantics: a transaction reads a stable snapshot, sees its own writes, validates expected revisions at commit, and atomically publishes its write set. It supports multiple active optimistic writers. A backend fixture may declare that it supports only one active writer; portable conformance cases do not require parallel writers, while conflict-at-commit cases run only for capable backends. `record_if_new` returns `true` only for a new identity. Render-intent and outbox `claim_batch` return each row with an opaque, monotonically fenced `ClaimToken` and lease expiry. `complete`/`acknowledge` succeed only for the currently active token; an expired or superseded worker receives `StaleClaim` and cannot overwrite a newer owner's result.

## Revisions, errors, and migration

`save` requires the caller's expected revision. A mismatch returns a typed conflict and leaves the record unchanged. Storage errors distinguish not-found, invalid input, conflict, stale claim, unavailable/timeout, serialization/schema failure, permission failure, revision exhaustion, and unknown commit outcome. Reads use a consistent transaction snapshot where the backend supports it.

Deletion advances the key generation and retains a tombstone. Recreating the same logical key
advances from that tombstone, so a transaction holding an older generation cannot overwrite the
new record (ABA protection). The in-memory baseline retains tombstones for the backend lifetime.
A durable adapter may compact them only with an equivalent generation registry or a proven safe
epoch policy that cannot admit a stale transaction; cleanup policy is adapter-specific and must be
covered by its conformance tests.

Schema versions are explicit per logical table/profile. Applications validate the expected schema
before serving traffic. The M2a reference backend rejects both validation and transaction intake
while metadata is `Migrating` or `RecoveryRequired`; only `Stable` accepts transactions.
Migrations are ordered, idempotent or resumable, recorded transactionally where possible, and
recovery states what happens after a crash during migration or commit.

## Recoverable command-to-delivery chain

Required state changes use synchronous `ctx.dispatch()` command handling inside the per-conversation serialization boundary. Asynchronous EventBus notifications may observe committed work, but cannot be the sole carrier of a required transition or outgoing effect.

Transaction 1 processes an update:

```text
begin UnitOfWork
  record_if_new(update_identity)
  load current session at revision r
  validate authorization/action against current state
  apply FSM transition and save with expected revision r
  append durable RenderIntent(conversation_key, new_revision)
commit
```

After commit, a renderer worker claims the intent, loads committed state, builds desired UI, and reconciles it against applied UI. Transaction 2 persists the result:

```text
begin UnitOfWork
  append planned OutboxEffect(desired_revision, operation)
  complete or supersede RenderIntent at expected intent revision
commit
```

A crash after transaction 1 leaves the render intent recoverable. A crash before transaction 2 commits leaves no visible effect and keeps the intent eligible. A crash after transaction 2 leaves a durable outbox row. Telegram HTTP is outside both transactions and may become `UnknownOutcome`. Independent physical databases have no cross-database atomicity guarantee.

## Mandatory versus optional services

Mandatory runtime persistence is `meta/schema`, sessions, processed updates, render intents, outbox, and technical event journal. User, chat, settings, permissions, jobs, and message history are optional application modules; M4/M5 may provide implementations. Message content is opt-in, retention-bound, and deletable.

## Initial MDBX profile

MDBX uses a calculated DBI budget. The minimal runtime profile requires **6 logical DBIs**: `meta`, `sessions`, `processed_updates`, `render_intents`, `outbox`, and `event_journal`. Optional modules declare their DBIs and secondary-index DBIs (for example users `+1`, chats `+1`, history data `+1` plus each enabled lookup index). Configuration computes `required_dbs = runtime_dbs + module_dbs + index_dbs + reserved_headroom` and fails before opening the environment if `max_dbs` is insufficient. It never creates a DBI per user/chat.

Logical keys use composite encodings such as `(bot_id, conversation_scope, chat_id, thread_id, user_id)` where applicable. Byte ordering is fixed before implementation so range scans are deterministic. Pagination uses bounded ranges and continuation keys. Transactions are short-lived, single-owner, and never hold network calls or user callbacks. MDBX reader/writer limits, map-size errors, and retryable conflicts become typed errors.

**Verification:** the M2a fixture-based conformance suite currently covers schema compatibility and
intake gates, stable snapshots/read-your-writes, one-increment repeated CAS writes, revision
boundaries, delete/recreate ABA rejection, rollback-on-destruction and move-assignment, read-only
rejection, duplicate creates, optimistic conflicts, atomic multi-record publication, ownership,
allocation-free no-op commits, and deterministic known/unknown failure injection. Parallel-writer
cases are capability-gated for
future single-writer backends. The two-transaction render-intent chain,
migrations beyond schema metadata, bounded queries, DBI-budget calculation, lease expiry/fencing,
and late-ack rejection remain M2b–M2e work. **Dependencies:** none in core; MDBX adapter is M2d.
