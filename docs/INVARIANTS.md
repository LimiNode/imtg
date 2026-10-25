# Architectural invariants

**Status:** normative. **Semantic owner:** core maintainers.

1. **Stable identity:** UI component IDs and `ConversationKey` are explicit, stable, and independent of translated text.
2. **Per-session ordering:** updates sharing a logical conversation are applied sequentially.
3. **No callback persistence:** executable callbacks and captured addresses never enter durable storage.
4. **Scoped lifetime:** `ViewContext`, `ActionContext`, subscriptions, and borrowed payloads cannot be used beyond their documented scope; in-flight callbacks are drained before their owners are destroyed.
5. **Separated effects:** events and state changes do not directly perform Telegram delivery; durable effects use an outbox.
6. **Backend neutrality:** core depends on storage contracts, never on MDBX/SQLite APIs.
7. **Revision safety:** writes use expected revisions or an equivalent conflict policy; silent lost updates are forbidden.
8. **Bounded work:** queues, retries, callback data, and rendered payloads have explicit limits.
9. **Locale isolation:** a user render uses an immutable locale/catalog snapshot; no global mutable language context exists.
10. **Honest status:** conceptual examples are not represented as implemented functionality.

Contract tests should name the invariant they protect. Exceptions require an ADR and an explicit impact analysis.
