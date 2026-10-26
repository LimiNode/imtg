# UI and actions contract

**Semantic owner:** UI/runtime maintainers. **Status:** contract-only.

## Responsibilities

The UI layer builds a transport-neutral tree from current state. It owns stable component identity, references to pre-registered actions, payload validation, and the boundary between view code and the runtime. It does not own Telegram HTTP, durable session state, or delivery retries.

The conceptual public surface uses `snake_case` (`ui.begin()`, `ui.text()`, `ui.button()`, `ui.input_text()`, `ui.end()`). Types use `CamelCase`. These snippets are not implemented API.

M1b chooses exception-safe RAII scopes for nested grouping. `UiBuilder::begin()`, `section()`, and `row()` return move-only scope guards; destruction closes the matching scope. A builder has no global context, may be used independently on parallel threads, and is invalidated after `finish()`. The builder must outlive all guards. Duplicate explicit child IDs are rejected, and `finish()` rejects active scopes rather than silently producing an unbalanced tree.

## Two kinds of actions

An **ephemeral callback binding** is a render-local helper that is never encoded into a Telegram control and cannot receive a later update. A **persistent registered action** is the only action kind permitted for a Telegram button: it is a named, versioned application route with a serializable payload schema and handler factory/callback registered during application composition before update intake.

During rendering, `ui.button(id, label, payload, action_handle)` computes component identity from the `ConversationKey`, screen identity, and component path, then emits a callback token for the handle's stable action ID/version and validated payload. The token contains an opaque token ID or encoded payload plus integrity/authentication data. It must fit the Telegram callback-data limit; oversized payloads are rejected or replaced by a durable token reference.

The application registry holds executable handlers in process memory and stable route metadata; durable storage holds only action ID/version, payload data (or a reference), binding constraints, and expiry. It never serializes C++ callbacks, captured pointers, references, or object addresses. Startup must rebuild and validate the complete route registry before accepting updates. A controller or handler is located by route and constructed or kept alive for the invocation. Registered handler captures are limited to application-lifetime services or immutable owned values; invocation references are synchronous and cannot escape.

## Resolution lifecycle

1. **Compose:** before intake, the application registers `save.v1` with its typed payload schema and handler, obtaining an `ActionHandle`.
2. **Render:** the controller reads a locale/session snapshot, passes that handle and payload to `ui.button()`, and the UI tree emits a signed token.
3. **Receive/schedule:** `UpdateGateway` checks token size, syntax, signature, bot identity, and derives the key; the scheduler serializes it.
4. **Authorize:** inside that boundary, `ActionRouter` loads current state and verifies expiry, user/chat/message binding, session revision policy, route version, and application authorization.
5. **Resolve/invoke:** the router validates the payload schema, finds the pre-registered handler, creates an action scope, and invokes it.
6. **Commit:** synchronous `ctx.dispatch()` commits the processed update, state change, and render intent. The callback itself is not persisted.
7. **Reconcile:** post-commit work produces a new desired UI revision and durable outbox effect. The old token is no longer preferred.

For example, conceptual application composition and rendering are:

```cpp
const auto delete_action = app.actions().register_action<DeleteItem>(
    "delete.v1",
    [](imtg::ActionContext& ctx, const DeleteItem& item) {
        ctx.dispatch(DeleteRequested{item.item_id});
    });

// In a later render, possibly after restart:
ui.button("delete", "Delete", DeleteItem{item_id}, delete_action);
```

After restart, application composition deterministically registers `delete.v1` again before intake. The old token carries that route/version and `item_id`; `ActionRouter` resolves it to the rebuilt registry entry without reconstructing the previous screen. A render pass is never used to discover executable handlers. If the route/version is no longer registered, the token is stale and fails closed.

## Stale and unsafe actions

Unknown routes, bad signatures, invalid payloads, expired sessions, deleted screens, mismatched message/chat/user, and unauthorized users fail closed. The runtime acknowledges or edits the UI only according to the delivery policy, records a technical event, and may render a current-screen explanation. It must not execute a handler merely because a token was once valid. A revision mismatch is either rejected as stale or explicitly allowed by the route's policy; it is never silently ignored.

`ViewContext` borrows read-only state and owns the render builder until render completion. `ActionContext` is valid only for one invocation. The router holds the controller/service scope and route registry entry until the handler and synchronous command work complete. Deferred work receives an owned serializable command instead.

**Verification:** deterministic tree snapshots; stable-ID and token tests; schema/version and signature tests; restart and route-registry reconstruction tests; stale/deleted/unauthorized action tests; callback lifetime tests; and a limit test for encoded payload size. **Dependencies:** sessions, events/lifetime, storage for persistent tokens, and delivery validation.
