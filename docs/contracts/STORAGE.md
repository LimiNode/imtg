# Storage contract

**Semantic owner:** persistence maintainers. **Status:** contract-only.

Core depends only on `IStorageBackend`, `ITransaction`, typed repositories, and `UnitOfWork` abstractions. A transaction has explicit ownership and RAII rollback on failure. Writes use expected revisions or an equivalent conflict result; silent lost updates are not permitted. Schema versions and migrations are explicit and testable.

Required logical services include session/FSM state, users, chats, scoped settings, permissions, technical journal, persistent jobs, outbox, and processed updates. Message history is optional, opt-in, retention-bound, and erasable. MDBX should use composite keys, indexes, range queries, and an explicit `max_dbs` budget; never allocate a DBI per user or chat. Atomicity across independent physical databases is not promised.

Planned backends are InMemory, MDBX via `mdbx-containers`, SQLite, and custom implementations. **Verification:** a backend conformance suite covers transactions, conflicts, migrations, failure recovery, bounded queries, retention deletion, and outbox durability. **Dependencies:** none in core; adapters are milestone work.
