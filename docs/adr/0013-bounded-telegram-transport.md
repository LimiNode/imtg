# ADR 0013: Bounded Telegram transport boundary

**Status:** Proposed with M3b-a

## Decision

The first Telegram delivery slice exposes a small `imtg::telegram_transport`
module with an injected `IHttpTransport`. `TelegramBotApi` constructs
authenticated form requests for `sendMessage`, `editMessageText`, and
`answerCallbackQuery`, while the application owns the HTTP implementation,
credentials, threads, and cancellation source.

Requests and responses have explicit byte limits. HTTP status classes and
`Retry-After` are returned as typed outcomes. A timeout, cancellation, body
limit, or malformed response that may follow a sent request is classified as
`UnknownOutcome`; the client never retries or claims idempotency. The module
does not call storage transactions and is not linked into `imtg_core` or the
transport-neutral M3a router.

## Consequences

- deterministic fake transports can exercise delivery policy without network
  access or credentials;
- `kurlyk` remains a future application-owned adapter rather than a core
  dependency;
- lost `sendMessage` responses cannot silently create a duplicate send;
- method-specific retry and outbox acknowledgement remain later M3b/M3c work.
