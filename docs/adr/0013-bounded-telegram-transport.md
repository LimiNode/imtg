# ADR 0013: Bounded Telegram transport boundary

**Status:** Proposed with M3b-a

## Decision

The first Telegram delivery slice exposes a small `imtg::telegram_transport`
module with an injected `IHttpTransport`. `TelegramBotApi` constructs
authenticated form requests for `sendMessage`, `editMessageText`, and
`answerCallbackQuery`, while the application owns the HTTP implementation,
credentials, threads, and cancellation source.

Requests and responses have explicit byte limits. HTTP status alone never
establishes Telegram success: bounded JSON parsing verifies `ok` and the
method-specific result, including the requested chat/message scope. Typed
results expose confirmed message identity, numeric API error code and coherent
retry delay, never untrusted descriptions. A timeout, cancellation, body limit,
or malformed response that may follow a sent request is `UnknownOutcome`; the
client never retries or claims idempotency. The module does not call storage
transactions and is not linked into `imtg_core` or the transport-neutral M3a router.

JSON syntax is delegated to nlohmann/json, not a new local parser. A callback
enforces a 32-level nesting bound and rejects duplicate keys; byte limits bound
the input before parsing. The selected JSON revision is the same source pin
used by the reviewed `kurlyk` baseline, kept private to the compiled adapter so
installed consumers do not need a JSON package or public JSON types.

## Protocol evidence

Reviewed against official Telegram documentation on 2026-11-20:

- [Making requests](https://core.telegram.org/bots/api#making-requests): HTTPS
  authenticated URL, form encoding, Boolean `ok`, `result`, and API errors;
- [sendMessage](https://core.telegram.org/bots/api#sendmessage): returns `Message`;
- [editMessageText](https://core.telegram.org/bots/api#editmessagetext): non-inline
  edits return `Message`; the inline `true` result is outside this client profile;
- [answerCallbackQuery](https://core.telegram.org/bots/api#answercallbackquery):
  returns `true`;
- [ResponseParameters](https://core.telegram.org/bots/api#responseparameters):
  `retry_after` is an integer seconds delay, not a delivery or idempotency proof.

## Consequences

- deterministic fake transports can exercise delivery policy without network
  access or credentials;
- `kurlyk` is an application-owned optional adapter rather than a core
  dependency; the concrete Draft integration is recorded in
  [ADR 0014](0014-kurlyk-http-implementation-boundary.md);
- lost `sendMessage` responses cannot silently create a duplicate send;
- method-specific retry and outbox acknowledgement remain later M3b/M3c work.

### Initial upstream blocking evidence

The originally reviewed `kurlyk@b62cd53f672c47b74bb14d762ea9dc59a9df19ce`
[receive handler](https://github.com/LimiNode/kurlyk/blob/b62cd53f672c47b74bb14d762ea9dc59a9df19ce/include/kurlyk/http/HttpRequestManager/HttpRequestHandler.hpp#L197)
appends chunks to the complete response before invoking the streaming callback.
It has no receive-time body cap, so streaming cancellation alone does not meet
this boundary's hard response limit. This initially deferred the concrete
adapter. Upstream corrective #50 now supplies the required receive/abort
facility; the dependent Draft implementation follows ADR 0014 and remains
gated on independent acceptance of that upstream change and flat-provider #51.
