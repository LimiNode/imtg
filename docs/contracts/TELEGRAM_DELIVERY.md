# Telegram delivery contract

**Semantic owner:** transport/delivery maintainers. **Status:** contract-only.

## Responsibilities

The pipeline has explicit boundaries:

1. **UpdateGateway** parses an inbound update, authenticates the bot boundary, validates size/shape, and normalizes Telegram identifiers.
2. **Conversation scheduler** serializes the derived `ConversationKey`; only stateless token syntax/integrity checks occur before it.
3. **ActionRouter/controller** loads current state inside the serialized boundary, validates expiry, conversation/message/user binding, revision policy and authorization, then transactionally applies the command.
4. **Render-intent store** commits the processed update, state transition, and durable need-to-render marker atomically.
5. **Renderer/Reconciler** consumes the committed intent, loads committed state, compares desired UI with the last applied revision, and plans transport-neutral effects; it does not execute handlers.
6. **Outbox** durably records planned effects in a second transaction and atomically marks or supersedes the render intent. A crash before this transaction leaves the intent eligible for reconciliation again.
7. **DeliveryWorker** executes Telegram HTTP requests, applies rate limits/backoff, and records outcomes with the current lease/fencing token.
8. **Acknowledgement/recovery** updates effect state and schedules review for uncertain results.

`kurlyk` is a recommended future HTTP adapter, not a core dependency. Telegram's external API cannot provide exactly-once delivery without corresponding guarantees, and arbitrary client idempotency keys must not be assumed.

## Effect state machine

Each durable effect has a desired revision and an explicit state: `Pending` (committed but not attempted), `InFlight`, `Applied` (response confirms success), `Failed` (non-retryable or exhausted), or `UnknownOutcome` (the request may have been accepted but the response is unavailable/ambiguous). Desired and applied UI revisions are separate; an external edit or stale worker cannot silently advance `Applied`.

Retrying `sendMessage` after a lost response can create a duplicate because Telegram may have accepted the first request and offers no arbitrary application idempotency key. New-message recovery therefore uses a bounded retry policy followed by `UnknownOutcome` and reconciliation/duplicate review; it must not claim exactly-once. Editing a known message is safer: retry the same edit with the known message identity, tolerate an already-applied result where detectable, and mark `UnknownOutcome` when message existence or content cannot be established. Repeated sends may require an application-level deduplication marker in content or a manual recovery policy, not an invented Telegram guarantee.

## Failure example

After transaction 1 commits a session transition and render intent, the process may crash before reconciliation; startup finds the unconsumed intent and safely plans the effect. After the outbox transaction commits, the worker may send `sendMessage` and lose the HTTP response. The effect becomes `UnknownOutcome`, not immediately `Pending` again. Telegram Bot API has no universal operation that reads an arbitrary message by `(chat_id, message_id)`, and a lost send response may not provide a message ID at all. Recovery may use only capabilities confirmed for that method/update, application-visible follow-up evidence, or manual policy; it must not assume generic message lookup. If acceptance cannot be distinguished, the runtime records the uncertainty and avoids unbounded duplicate sends. A later UI revision may supersede desired state, but the technical journal retains the original outcome.

**Verification:** fake transport tests for inbound validation, authorization, send/edit/retry, 429/backoff, uncertain outcomes, revision races, duplicate prevention policy, malformed callbacks, and shutdown with pending outbox records. **Dependencies:** UI/actions, sessions, storage/outbox, and verified official Telegram API limits.
