# Sessions and FSM contract

**Semantic owner:** session/runtime maintainers. **Status:** contract-only.

## Minimum conversation identity

Before M1, `ConversationKey` is defined as a value containing:

```text
bot_id + chat_id + session_scope
```

`chat_id` identifies the Telegram chat. `bot_id` prevents two bot identities from sharing state. `session_scope` is a typed, serialized discriminator with a default of `Chat`; supported planned values are `Chat`, `UserInChat(user_id)`, and `Thread(thread_id)`. Telegram user identity is distinct from chat identity; a private chat defaults to `Chat` because the chat already identifies the user. A group defaults to one chat-wide session. Applications that need independent conversations per participant select `UserInChat`; forum/topic flows select `Thread` and may additionally include a user in application state. The chosen scope is part of the persisted key and cannot be inferred from translated labels.

The minimum M1 scheduler must support `Chat` and `UserInChat`; `Thread` is a reserved, serializable variant whose Telegram mapping is completed before M3. Equality and byte encoding of every variant are tested before scheduler implementation. This closes the identity blocker without preventing later scope variants.

## Session and transitions

A session contains FSM state, current screen metadata, revision, selected scope, pending-input metadata, and timestamps. It does not contain the user profile, mutable global locale, or message-history content. A transition consumes a typed event, evaluates guards against a consistent model snapshot, and returns either a new state/effects or a typed rejection. Timeouts and cancellation are explicit transitions.

Updates for one `ConversationKey` are sequential; unrelated keys may run concurrently. Before scheduling, only stateless update/token validation and key derivation are permitted. Session loading, authorization, stale-revision checks, action resolution, and transition handling occur inside the serialized boundary and are revalidated immediately before commit. A persisted transition uses expected revision and the storage transaction boundary. On restart the session is loaded by exact key and schema version; unknown/expired states fail closed and are journaled. Processed-update deduplication, FSM transition, and durable render intent commit atomically; post-commit reconciliation converts the intent into an outbox effect as described in [STORAGE.md](STORAGE.md).

**Verification:** key encoding/equality tests for private, group, per-user, and thread scopes; transition/guard/timeout tests; per-key ordering and concurrent-key isolation tests; two callbacks racing on one revision with the second rejected after in-boundary revalidation; restart/migration tests; and stale callback revision tests. **Dependencies:** UI/actions, storage, events/lifetime, and the M1 scheduler.
