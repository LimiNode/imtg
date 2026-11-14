# Multi-surface application contract

**Status:** proposed, documentation-only. **Semantic owner:** application
composition maintainers. **Scope:** applications that expose one domain model
through a Telegram bot, a Telegram Mini App, and an optional operator console.

This contract does not add an HTTP server, Telegram transport, WebAssembly
runtime, GUI dependency, or application-specific domain service to `imtg_core`.
It defines the boundaries that an application adapter must preserve.

## Purpose and terminology

One application may expose several surfaces:

| Surface | Boundary | Typical authority |
| --- | --- | --- |
| Telegram bot | Telegram Gateway and Bot API | Interactive conversation and notifications |
| Telegram Mini App | HTTPS Application API | User catalog, forms, cart and order commands |
| Operator console | Authenticated Operator API | Privileged administration and operations |

These surfaces are different interfaces to one application. They are not
three copies of business logic and do not have to run in three processes. A
deployment may host all adapters in one C++ process or place the static Mini
App and API behind a separate HTTPS service. Process placement is a deployment
choice; authority and authorization boundaries are not.

The shared application/domain layer owns commands, queries, authorization,
transactions, domain events and projections. `imtg_core` supplies reusable
runtime, storage, session/FSM, action and delivery contracts. A concrete
application owns services such as `CatalogService`, `CartService`,
`OrderService` and `UserService`; these names are examples, not `imtg_core`
types.

## State ownership

Business state is shared across surfaces. Examples include products, carts,
orders, payments, subscriptions, balances and permissions. It is stored and
changed by domain services through the authoritative storage transaction.

Conversation state is different. The current bot screen, FSM state, pending
input, Telegram UI revision and `ConversationKey` belong to the bot runtime.
They are not a public write API for a Mini App or operator client. A Mini App
command may cause a domain event such as `OrderPlaced`; a bot adapter may then
schedule a notification or a render intent. The Mini App must not directly
write arbitrary `SessionRecord` fields or call an unrestricted
`set_fsm_state()` operation.

An operator projection is also not authoritative. It is a bounded, revisioned
cache of backend state. A late response or a projection from another bot,
shop, merchant account or runtime must never overwrite the selected context.

## Surface boundaries

```text
Telegram Bot      -> Telegram Gateway -> commands/queries -> shared domain layer
Telegram Mini App -> HTTPS Application API -------------> shared domain layer
ImGuiX Admin      -> authenticated Operator API --------> shared domain layer
                                                              |
                                                      storage / commerce
```

The Application API is for the authenticated end user and exposes only user-
scoped operations. The Operator API is a separate security boundary with
operator, runtime, bot, shop and merchant scopes. Sharing a C++ process or a
transport implementation does not merge these authorization policies.

An API adapter translates a request into a typed command or query. It does
not call payment-provider SDKs, mutate storage records directly, publish an
untyped event as a required state transition, or construct a Telegram UI tree
as a substitute for a domain command.

## Mini App identity and authentication

The Mini App client sends the Telegram-provided `Telegram.WebApp.initData` to
the backend over HTTPS. The backend verifies the signed data using the
documented Telegram algorithm, checks freshness and replay policy, binds the
result to the expected bot/application, and only then derives the authenticated
Telegram user and optional start context.

`initDataUnsafe`, a client-supplied `user_id`, a client-supplied `shop_id`, or
any browser redirect is not authentication or payment evidence. The adapter
may verify signed `initData` on every request or exchange it for a short-lived
server session. A server session must retain the authenticated bot, user,
expiry and authorization context; it must not become a bearer substitute for
fresh validation beyond its stated lifetime.

The application must bind every command to an explicit user, bot and domain
scope. It must reject expired, malformed, replayed, cross-bot and cross-shop
contexts before invoking a domain service. Secrets, raw init data and payment
credentials are redacted from ordinary logs.

The normative implementation must track the current Telegram Web Apps
documentation before shipping an adapter:
[init data validation](https://core.telegram.org/bots/webapps#validating-data-received-via-the-mini-app),
[Web Apps API](https://core.telegram.org/bots/webapps), and
[`sendData`](https://core.telegram.org/bots/webapps#initializing-mini-apps). The
`sendData` path is an optional small-form bridge; it does not replace the
HTTPS Application API and is not assumed for catalog, cart or checkout flows.

## Commands, queries and consistency

Queries return bounded, revisioned projections. Commands carry:

- authenticated principal and explicit bot/shop/merchant scope;
- a bounded request body and a client request identity;
- an idempotency key for non-read operations;
- the observed resource revision or an explicit create expectation;
- a deadline/timeout budget that is local accounting, not a Telegram guarantee.

The domain service rechecks authorization and expected revisions inside the
serialized/keyed transaction boundary. A stale revision returns a typed
conflict and does not silently merge a lost update. Repeating a command with
the same idempotency key returns the original committed outcome; reusing that
key for a different command or scope is a conflict.

The existing scheduler and storage/CAS contracts are reused where a command
touches a `ConversationKey` or another serialized domain key. The Application
API does not require a second generic event framework. Asynchronous events are
notifications; a required command, processed-update decision, payment status
change, or outbox publication must be committed through the domain/storage
path.

## Synchronization with the bot

After a Mini App command commits shared business state, the application may
publish a typed domain event. A Telegram adapter can consume it and create a
bot notification or render intent through the existing outbox/reconciliation
contracts. The order is:

```text
Mini App request
  -> authenticate and authorize
  -> domain transaction: business state + idempotency record + event/intent
  -> commit
  -> optional bot notification through outbox
```

The browser closing, invoice window closing, redirect, or a successful HTTP
transport response is not proof of payment. Payment verification and
fulfillment remain governed by `PAYMENTS.md`; only an authoritative verified
provider event may advance payment state or start fulfillment.

The reverse path is symmetric: a bot command changes business state through
the same domain service, and a later Mini App query reads the new revision.
Push updates (SSE/WebSocket) are optional projections; polling a bounded query
is always a valid fallback. No surface is permitted to infer state from a
stale local projection.

## Operator boundary

The Operator API is not an elevated Application API route. It must define
separate authentication, TLS or local-channel policy, per-operator scopes,
audit records, bounded requests/responses, rate limits, reconnect behavior and
redaction. Destructive, financial, permission and moderation commands require
explicit confirmation and idempotency.

An ImGuiX client may use local models and controllers, but it sends versioned
DTOs rather than ImGuiX event objects, pointers, storage records or provider
SDK types. A selected runtime/bot/shop context is part of every request and
projection. Changing it invalidates incompatible drafts and pending responses.
See [OPERATOR_CONSOLE.md](OPERATOR_CONSOLE.md) for the existing console
contract and [ADR 0008](../adr/0008-operator-admin-console.md).

## Failure and lifecycle rules

- Authentication, authorization, scope, size and freshness failures fail
  closed before domain execution.
- `QueueFull`, `DeadlineMissed`, `Closed`, storage conflict and unknown commit
  outcomes are observable typed outcomes; none is reported as success.
- A request that reaches an unknown storage commit is recovered by authoritative
  reads using its request/idempotency identity. The adapter does not replay a
  non-idempotent command blindly.
- A lost connection does not turn a command into success. After reconnect the
  client refreshes the authoritative projection and reports unresolved request
  outcomes.
- Shutdown stops intake and leaves committed domain/outbox work recoverable;
  an adapter does not discard pending commands or claim that callbacks stopped
  merely because a client disconnected.

## Verification and non-goals

An implementation PR must add deterministic tests for:

1. signed `initData` validation, freshness, replay and cross-bot rejection;
2. Application API versus Operator API scope separation;
3. duplicate idempotency keys and conflicting reuse;
4. stale revisions, concurrent commands and authoritative re-read after
   `Unknown` commit;
5. Mini App command followed by one shared-state bot read/notification;
6. bot command followed by one shared-state Mini App read;
7. no direct Mini App mutation of bot FSM/session state;
8. bounded projections, reconnect, timeout and shutdown behavior.

This contract does not define catalog, cart, order, inventory, moderation or
provider-specific schemas. It does not make Mini Apps, HTTP, WebSockets,
JavaScript, Emscripten, WebAssembly, ImGuiX or a new EventBus dependencies of
`imtg_core`. It does not claim that a shared semantic view can be rendered
identically by Telegram messages and a graphical canvas.
