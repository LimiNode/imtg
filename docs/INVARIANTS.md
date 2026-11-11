# Architectural invariants

**Status:** normative. **Semantic owner:** core maintainers.

1. **Stable identity:** UI component IDs and `ConversationKey` are explicit, stable, and independent of translated text.
2. **Per-session ordering:** updates sharing a logical conversation are scheduled before state-dependent authorization and applied sequentially.
3. **No callback persistence:** executable callbacks and captured addresses never enter durable storage.
4. **Scoped lifetime:** `ViewContext`, `ActionContext`, subscriptions, and borrowed payloads cannot be used beyond their documented scope; in-flight callbacks are drained before their owners are destroyed.
5. **Recoverable effects:** committing a processed update and state transition also commits a durable render intent; reconciliation converts that intent into an outbox effect without a loss window.
6. **Separated commands and events:** required state changes use synchronous transactional commands; asynchronous EventBus notifications do not determine whether an update is processed.
7. **Backend neutrality:** core depends on storage contracts, never on MDBX/SQLite APIs.
8. **Revision safety:** writes and leased acknowledgments use expected revisions/fencing; silent lost updates are forbidden.
9. **Bounded work:** queues, retries, callback data, and rendered payloads have explicit limits.
10. **Locale isolation:** a user render uses an immutable locale/catalog snapshot; no global mutable language context exists.
11. **Honest status:** conceptual examples are not represented as implemented functionality.
12. **One domain authority:** bot, Mini App and operator surfaces use the same
    authoritative business services and storage; they do not maintain separate
    carts, orders, payment rules or permissions.
13. **State separation:** a user-facing Application API may request a domain
    command but cannot directly mutate bot `SessionRecord`, FSM or render-intent
    state. Conversation state remains fenced by `ConversationKey` and runtime
    ownership.
14. **Surface authentication:** signed Telegram Mini App launch data is
    verified server-side and bound to bot/user/shop scope. `initDataUnsafe`,
    client IDs, redirects and browser callbacks are not authentication or
    payment evidence. Operator access has a separate privileged boundary.
15. **Cross-surface consistency:** commands carry idempotency and revision
    expectations; stale, duplicate, conflicting and unknown outcomes remain
    explicit. Notifications never replace the authoritative transaction.

Contract tests should name the invariant they protect. Exceptions require an ADR and an explicit impact analysis.
