# Sessions and FSM contract

**Semantic owner:** session/runtime maintainers. **Status:** contract-only.

`ConversationKey` is an explicit, stable value identifying the logical interaction scope; it is not inferred from translated text or a mutable global. A session contains FSM state, current screen metadata, revision, and pending input metadata. User profile data and message history are separate records.

Transitions are typed and may have guards, timeout policies, and rejection outcomes. Updates for one key are sequential; unrelated keys may run concurrently. A persisted transition uses expected revision and is recoverable after restart. Expired or unknown states fail closed and are recorded for diagnosis.

**Verification:** transition-table tests, guard/timeout tests, per-key ordering tests, migration/recovery tests, and concurrent-key isolation tests. **Dependencies:** storage and event contracts.
