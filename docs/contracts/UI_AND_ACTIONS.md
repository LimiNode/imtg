# UI and actions contract

**Semantic owner:** UI/runtime maintainers. **Status:** contract-only.

## Responsibilities

The UI layer builds a transport-neutral tree from current state. It owns stable component identity, action registration for a render, payload validation, and the boundary between view code and the runtime. It does not own Telegram HTTP, durable session state, or delivery retries.

The conceptual public surface uses `snake_case` (`ui.begin()`, `ui.text()`, `ui.button()`, `ui.input_text()`, `ui.end()`). Types use `CamelCase`. These snippets are not implemented API.

## Two kinds of actions

An **ephemeral callback binding** is a render-scoped handler used while resolving one incoming action. It may validate input or dispatch a typed event, but it is not durable and cannot be invoked after its action scope ends. A **persistent registered action** is a named, versioned route with a serializable payload schema. The route and payload are sufficient to resolve a handler after a render, process restart, or controller reconstruction.

During rendering, `ui.button(id, label, payload, handler)` computes an action identity from the `ConversationKey`, screen identity, component path, action name, and an application-defined schema/version. It registers a temporary binding in the in-memory render context and emits a callback token containing an opaque token ID, action ID/version, payload encoding, and integrity/authentication data. The token must fit the Telegram callback-data limit; oversized payloads are rejected or replaced by a durable token reference.

The persistent action registry stores only serializable route metadata and payload data (or a reference to data in storage). It never stores executable C++ callbacks, captured pointers, references, or object addresses. A controller or handler is located by route in the application registry and is constructed or kept alive for the invocation. Captures are limited to immutable value copies; references are permitted only for the synchronous call and must not escape it.

## Resolution lifecycle

1. **Render:** the controller reads a locale/session snapshot and registers `save.v1` with a typed payload, while the UI tree emits a signed token.
2. **Receive:** `UpdateGateway` parses the Telegram update and checks token size, syntax, signature, expiry, bot identity, and `ConversationKey` binding.
3. **Authorize:** `ActionRouter` verifies the user/chat/message binding, session revision or permitted stale policy, screen/action version, and application authorization.
4. **Resolve:** the router decodes and validates the payload schema, finds the route in the registry, and creates an action-scoped `ActionContext` and controller/service scope.
5. **Invoke:** the handler performs bounded validation and dispatches a typed event or command. It cannot retain the context or borrowed payload.
6. **Commit:** the event handler changes state using a transaction and, when needed, writes an outbox effect. The callback itself is not persisted.
7. **Render:** the resulting state produces a new desired UI revision and new tokens. The old token is no longer preferred.

For example, a conceptual action is:

```cpp
ui.button("delete", "Delete", DeleteItem{item_id},
    [](imtg::ActionContext& ctx, const DeleteItem& item) {
        ctx.dispatch(DeleteRequested{item.item_id});
    });
```

The stable `delete.v1` route and `item_id` payload survive; the lambda does not. After restart, the application registers `delete.v1` again and resolves the stored token through that route.

## Stale and unsafe actions

Unknown routes, bad signatures, invalid payloads, expired sessions, deleted screens, mismatched message/chat/user, and unauthorized users fail closed. The runtime acknowledges or edits the UI only according to the delivery policy, records a technical event, and may render a current-screen explanation. It must not execute a handler merely because a token was once valid. A revision mismatch is either rejected as stale or explicitly allowed by the route's policy; it is never silently ignored.

`ViewContext` borrows read-only state and owns the render builder until render completion. `ActionContext` is valid only for one invocation. The router holds the controller/service scope and route registry entry until the handler returns and all synchronous event work completes. Deferred work receives an owned serializable command instead.

**Verification:** deterministic tree snapshots; stable-ID and token tests; schema/version and signature tests; restart and route-registry reconstruction tests; stale/deleted/unauthorized action tests; callback lifetime tests; and a limit test for encoded payload size. **Dependencies:** sessions, events/lifetime, storage for persistent tokens, and delivery validation.
