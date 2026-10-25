# Telegram delivery contract

**Semantic owner:** transport/delivery maintainers. **Status:** contract-only.

## Responsibilities

The pipeline has explicit boundaries:

1. **UpdateGateway** parses an inbound update, authenticates the bot boundary, validates size/shape, and normalizes Telegram identifiers.
2. **ActionRouter** validates callback token integrity, expiry, conversation/message/user binding, session revision policy, and authorization before action resolution.
3. **Controller/runtime** applies the action and commits application state.
4. **Renderer/Reconciler** compares desired UI with the last applied revision and plans transport-neutral effects; it does not validate users or execute handlers.
5. **Outbox** durably records effects after the state transaction commits.
6. **DeliveryWorker** executes Telegram HTTP requests, applies rate limits/backoff, and records outcomes.
7. **Acknowledgement/recovery** updates effect state and schedules review for uncertain results.

`kurlyk` is a recommended future HTTP adapter, not a core dependency. Telegram's external API cannot provide exactly-once delivery without corresponding guarantees, and arbitrary client idempotency keys must not be assumed.

## Effect state machine

Each durable effect has a desired revision and an explicit state: `Pending` (committed but not attempted), `InFlight`, `Applied` (response confirms success), `Failed` (non-retryable or exhausted), or `UnknownOutcome` (the request may have been accepted but the response is unavailable/ambiguous). Desired and applied UI revisions are separate; an external edit or stale worker cannot silently advance `Applied`.

Retrying `sendMessage` after a lost response can create a duplicate because Telegram may have accepted the first request and offers no arbitrary application idempotency key. New-message recovery therefore uses a bounded retry policy followed by `UnknownOutcome` and reconciliation/duplicate review; it must not claim exactly-once. Editing a known message is safer: retry the same edit with the known message identity, tolerate an already-applied result where detectable, and mark `UnknownOutcome` when message existence or content cannot be established. Repeated sends may require an application-level deduplication marker in content or a manual recovery policy, not an invented Telegram guarantee.

## Failure example

After committing a session transition and `sendMessage` outbox record, the worker sends the request and loses the HTTP response. The effect becomes `UnknownOutcome`, not immediately `Pending` again. A bounded recovery job queries available Telegram state or renders a reconciliation decision; if it cannot distinguish acceptance, it records the uncertainty and avoids unbounded duplicate sends. A later UI revision may supersede the pending desired state, but the technical journal retains the original outcome.

**Verification:** fake transport tests for inbound validation, authorization, send/edit/retry, 429/backoff, uncertain outcomes, revision races, duplicate prevention policy, malformed callbacks, and shutdown with pending outbox records. **Dependencies:** UI/actions, sessions, storage/outbox, and verified official Telegram API limits.
