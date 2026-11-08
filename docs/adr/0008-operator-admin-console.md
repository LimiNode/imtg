# ADR 0008: optional operator admin console

**Status:** Proposed; documentation-only. **Scope:** future `imtg-admin`
application, not `imtg_core`.

## Context

Managing several Telegram bots and shops will eventually benefit from a
desktop operator panel. The panel needs a modern immediate-mode UI, stable
navigation, local projections, reconnect handling and a secure remote API. It
must not become a second authority for payment, order, moderation or runtime
state, and it must not force GUI dependencies on headless deployments.

ImGuiX is a candidate for the UI framework. The GUI-shell and
model/controller/process-lifecycle organization in `mgc-platform` is a useful
reference. Neither project is part of the current dependency graph.

## Decision

Create a future, optional `imtg-admin` application with these boundaries:

1. **UI framework:** ImGuiX may provide windows, widgets and rendering. Its
   event bus remains inside the GUI process.
2. **Application shell:** `AdminApplication` owns configuration and lifecycle;
   `MainWindow` owns navigation and selected bot/shop context.
3. **Model/controller split:** controllers translate UI intent to commands;
   models maintain bounded local projections and request state; neither calls a
   payment provider or storage backend directly.
4. **Remote-first integration:** `OperatorApiClient` connects to an
   authenticated HTTP/WebSocket operator API. A local child-process controller
   is an optional deployment mode, not the primary architecture.
5. **Typed wire DTOs:** commands, queries and events use versioned DTOs with
   request IDs, idempotency keys, expected revisions and explicit context.
6. **Backend authority:** runtime, commerce, storage and authorization remain
   server-side authorities. The GUI repeats no business decision locally.
7. **Isolation:** bot, shop, merchant account, runtime and operator scopes are
   part of every projection and command; late or stale responses are rejected.

## Consequences

Positive:

- remote operation of multiple bots and shops without stopping them when the
  GUI closes;
- headless/runtime installations remain free of GUI dependencies;
- payment and moderation actions keep one authoritative backend path;
- models and controllers can be tested without a window system;
- ImGuiX can be replaced without changing backend contracts.

Costs and limits:

- an authenticated operator API, authorization model and audit journal are
  required before production GUI work;
- projections can be stale and must expose reconnect/unknown states;
- local process supervision adds lifecycle and log-attachment complexity;
- a GUI target needs its own dependency, packaging and platform policy.

## Rejected alternatives

1. **Embedding ImGuiX in `imtg_core`:** would make headless/runtime users pay
   for a presentation dependency and mix application concerns into the core.
2. **Direct controller-to-provider calls:** bypasses backend authorization,
   revision fencing, audit and payment recovery.
3. **Local process as the only integration:** cannot manage remote runtimes or
   survive GUI restarts as the primary operator workflow.
4. **Sending GUI EventBus objects over the network:** couples the wire contract
   to a UI framework and loses versioned, bounded DTO semantics.
5. **Copying all of `mgc-platform`:** imports unrelated commerce/UI
   capabilities; only the shell and lifecycle ideas are reusable references.

## Roadmap gates

The console is a later optional application track. It may be prototyped after
the operator API, M3 gateway boundaries and M4 composition/localization
contracts are accepted; durable screens depend on M5 services. No GUI source,
ImGuiX dependency or operator transport is added by this ADR.

See [OPERATOR_CONSOLE.md](../contracts/OPERATOR_CONSOLE.md) for the normative
contract baseline.
