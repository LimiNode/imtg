# Storage contract

**Semantic owner:** persistence maintainers. **Status:** contract-only; signatures below are conceptual and not implemented.

## Core interfaces

The core depends only on these contracts:

```cpp
// Conceptual API; illustrative only.
class IStorageBackend {
public:
    virtual Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) = 0;
};

class ITransaction {
public:
    virtual Result<void> commit() = 0;
    virtual void rollback() noexcept = 0;
};

class UnitOfWork {
public:
    explicit UnitOfWork(IStorageBackend& backend);
    Result<void> commit(); // rollback on destruction unless committed
};

class ISessionStore {
public:
    Result<SessionRecord> load(const ConversationKey&, ITransaction&);
    Result<void> save(const SessionRecord&, Revision expected, ITransaction&);
};

class IOutboxStore {
public:
    Result<OutboxId> append(OutboxEffect, ITransaction&);
    Result<std::vector<OutboxEffect>> claim_batch(ClaimToken, ITransaction&);
    Result<void> acknowledge(OutboxId, DeliveryOutcome, ITransaction&);
};

class IProcessedUpdateStore {
public:
    Result<bool> record_if_new(UpdateIdentity, ITransaction&);
};
```

Names, result types, and exact ownership may change during M1, but the semantics below are normative. A transaction is created and destroyed by its owning worker; it is not shared across threads. Repository calls borrow the transaction and do not commit independently. `UnitOfWork` rolls back on destruction unless commit succeeds. A failed commit has an explicit `CommitOutcome` (`Committed`, `RejectedConflict`, `Failed`, or `Unknown`) and must not be silently retried as if nothing happened.

The in-memory reference backend uses snapshot/optimistic semantics: a transaction reads a stable snapshot, sees its own writes, and validates all expected revisions at commit. Commit publishes the write set atomically; rollback publishes nothing. `record_if_new` returns `true` only when it inserted the update identity in the transaction, and a duplicate returns `false` without changing state. `claim_batch` leases only eligible outbox rows with an owner and expiry; `acknowledge` requires the lease token and records the terminal/uncertain outcome. A lease expiry makes a non-terminal row eligible again under its retry policy.

## Revisions, errors, and migration

`save` requires the caller's expected revision. A mismatch returns a typed conflict and leaves the record unchanged. Storage errors distinguish not-found, invalid input, conflict, unavailable/timeout, serialization/schema failure, permission failure, and unknown commit outcome. Reads use a consistent transaction snapshot where the backend supports it.

Schema versions are explicit per logical table/profile. Migrations are ordered, idempotent or resumable, recorded transactionally where possible, and validated before the application serves traffic. Recovery documentation must state what happens after a process crash during migration or commit.

## Atomicity boundary

For one backend transaction, processing an update is conceptually:

```text
begin UnitOfWork
  record_if_new(update_identity)
  load session at revision r
  validate and apply FSM transition
  save session with expected revision r
  append outbox effect for the resulting UI/delivery work
commit
```

The three durable records are atomic within that one physical backend transaction: either all are committed or none are visible. Telegram HTTP is outside this boundary; it is represented by the outbox and can be `UnknownOutcome`. Independent physical databases are never promised cross-database atomicity.

## Mandatory versus optional services

Mandatory runtime persistence is limited to processed-update deduplication, session/FSM state, schema metadata, technical event journal, and outbox records needed for recovery. User, chat, settings, permissions, jobs, and message-history services are replaceable application services; M4/M5 may provide ready implementations but the runtime must not require all of them. Message content remains opt-in, retention-bound, and deletable.

## Initial MDBX profile

The first MDBX adapter should use a small, explicit DBI budget rather than one DBI per user/chat. A starting profile is **8 DBIs**: `meta`, `sessions`, `processed_updates`, `outbox`, `event_journal`, `users`, `chats`, and `app_data`; optional history/jobs may use additional configured DBIs or composite-key tables after measurement. The profile must fail configuration if `max_dbs < 8` and reserve capacity for enabled optional services rather than silently creating DBIs.

Logical keys use composite encodings such as `(bot_id, conversation_scope, chat_id, thread_id, user_id)` where applicable. Define byte ordering before implementation so range scans are deterministic. Indexes and secondary lookup tables are explicit; pagination uses bounded ranges and continuation keys. Transactions are short-lived, single-owner, and must not hold network calls or user callbacks. MDBX reader/writer limits, map-size errors, and retryable write conflicts are surfaced as typed storage errors.

**Verification:** an in-memory reference backend and all future adapters must pass a conformance suite covering transaction rollback/commit outcomes, expected revisions, update deduplication, the atomic transition/outbox example, migrations, bounded range queries, failure recovery, retention deletion, and DBI-budget validation. **Dependencies:** none in core; MDBX adapter is M2 work.
