# ADR 0010: multi-surface application runtime

**Status:** Proposed; documentation-only. **Scope:** application composition
around `imtg_core`, `imtg_commerce` and future adapters. **Depends on:** ADR
0006 headless routing/application composition, ADR 0008 operator console,
M2 storage/reconciliation contracts, and the provider-neutral payment contract.

## Context

`imtg` is useful beyond a single Telegram chat UI. A shop or service may need
a Telegram bot for notifications and quick actions, a Telegram Mini App for
catalog/cart/forms, and an operator console for privileged administration.
Treating the Mini App as a component that directly changes the bot's FSM or
presentational state would create two authorities and permit a user-facing
client to bypass the bot's authorization boundary.

The surfaces also have different technical constraints. Telegram Bot API
messages and buttons are not a browser DOM or a WebGL canvas. A Mini App is a
web client with Telegram launch context. An ImGuiX panel is a privileged
operator client. They can share domain semantics, but they cannot share every
widget, lifecycle or permission.

## Decision

Model the bot, Mini App and operator console as separate interfaces over one
application/domain layer:

```text
Telegram Gateway  ─┐
Application API   ──┼──> Commands / Queries / Authorization / Transactions
Operator API      ─┘                         │
                                  Domain services and projections
                                             │
                                   Storage / Commerce / Outbox
```

The interfaces may be hosted in one process or separately. The shared layer,
not a surface adapter, owns business state and rules. Domain services are
application-owned or optional modules; `imtg_core` remains a backend- and
transport-neutral library.

### State separation

Business state (catalog, cart, order, payment, subscription, balance and
permissions) is shared. Bot conversation state (screen, FSM, pending input,
Telegram UI revision and `ConversationKey`) remains runtime-owned. A Mini App
or operator command can request a domain change, but cannot directly rewrite a
`SessionRecord`, arbitrary FSM state or a render-intent row.

If a business command should notify the bot, the committed domain event is
translated into a typed bot command/notification and then delivered through
the existing outbox/reconciliation boundary. Headless commands do not create
UI trees merely to share a notification path.

### Security boundaries

The Application API authenticates end users with server-verified signed
`Telegram.WebApp.initData` (or a short-lived server session created after that
verification). It never trusts `initDataUnsafe`, a supplied user ID, a supplied
shop ID or a browser redirect as proof. Every request is bound to the expected
bot and explicit user/shop scope.

The Operator API is a separate privileged boundary. It has its own
authentication, authorization, audit, redaction and transport policy. An
operator's selected runtime, bot, shop and merchant account are part of the
request context. Context changes invalidate incompatible drafts and late
responses.

### Consistency boundaries

All surfaces use typed commands and bounded revisioned queries. Non-read
commands carry an idempotency key and expected revision/create expectation.
The domain transaction rechecks authorization and CAS inside the keyed
scheduler/storage boundary. The existing EventBus remains notification-only;
it is not a required command transport and no new generic event framework is
introduced.

Mini App to bot synchronization is therefore:

```text
HTTPS command -> authenticate -> domain transaction -> commit
              -> optional typed event -> bot outbox notification
```

Bot to Mini App synchronization is the same shared-state path in reverse.
Polling is a valid baseline; SSE/WebSocket updates are optional projections.
The client-visible HTTP response or payment-window lifecycle is not proof of
payment. `PAYMENTS.md` and the authoritative provider event govern payment
state and fulfillment.

## ImGuiX and WebAssembly boundary

ImGuiX/WASM is an optional frontend research direction, not a core dependency
or an accepted Mini App implementation. A graphical canvas can reuse domain
controllers, models and semantic view data, but Telegram message UI remains a
separate renderer with a smaller capability set. Shared semantic templates may
be introduced only where their accessibility, input and lifecycle behavior is
defined; a universal Telegram/UI DSL is not assumed.

The research gates are recorded in
[IMGUIX_WASM_MINI_APP.md](../research/IMGUIX_WASM_MINI_APP.md). They require a
real ImGuiX application/controller proof, a minimal Telegram WebApp bridge,
mobile WebView smoke tests and an explicit decision about HTML accessibility.
The current ImGuiX Emscripten demo is not evidence that this contract is
implemented.

## Consequences

Positive:

- one authoritative business state and one set of domain rules;
- bot/FSM state stays scoped and cannot be edited by arbitrary web requests;
- Mini App and operator clients can evolve independently of Telegram message
  rendering and core storage contracts;
- payment, idempotency, CAS, outbox and UnknownOutcome rules are reused;
- a one-process deployment remains possible without forcing one process.

Costs and limits:

- each surface needs an adapter, authentication and bounded DTO policy;
- shared domain services need explicit scopes and concurrency tests;
- UI reuse is semantic, not pixel-identical;
- Telegram/WebView, browser, HTTP and operator security remain adapter work;
- no exact-once remote effect is implied by shared state or idempotency keys.

## Rejected alternatives

1. **Mini App writes bot FSM/session rows directly:** violates ownership,
   authorization and `ConversationKey` fencing.
2. **Separate business logic for bot and Mini App:** creates divergent carts,
   payment rules and reconciliation paths.
3. **Make `imtg_core` an HTTP/WASM server or GUI framework:** adds transport,
   browser and rendering dependencies to headless runtime users.
4. **One universal widget DSL for Bot API and ImGuiX:** hides real capability,
   accessibility and lifecycle differences and weakens both renderers.
5. **Use EventBus as the cross-surface command bus:** loses transactional
   ownership, idempotency and authoritative conflict handling.

## Gates

This ADR remains proposed until an implementation contract is independently
reviewed. Before calling the direction implemented, deterministic tests must
prove signed Mini App identity, scope isolation for two bots and two shops,
duplicate/conflicting commands, stale revision handling, unknown commit
recovery, bot/Mini App shared-state round-trip, and no direct FSM mutation.
The ImGuiX/WASM path additionally needs the research gates, but it is optional
and must not block the first HTML Mini App integration.
