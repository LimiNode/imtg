# ImGuiX/WASM Mini App research note

**Status:** exploratory, not an implementation commitment. **Semantic owner:**
frontend/integration maintainers. This note records a possible integration of
[ImGuiX](https://github.com/LimiNode/ImGuiX) with a Telegram Mini App; it does
not add ImGuiX, Emscripten, SDL2, WebGL or JavaScript dependencies to `imtg`.

## Hypothesis

An application may compile an ImGuiX application/controller/model layer to
WebAssembly and host it in an HTML shell inside a Telegram WebView. A small
JavaScript bridge could expose the Telegram Web Apps API for launch data,
viewport/theme state, BackButton and payment-window integration. The backend
still authenticates signed `initData`; WASM code and browser data are not an
authorization boundary.

This is an optional second Mini App presentation. A conventional HTML/CSS/
JavaScript client remains a first-class and often more practical choice for
mobile forms, accessibility and small payloads.

## What the existing evidence means

The ImGuiX repository has an Emscripten/SDL2/WebGL demonstration and a browser
shell. That is useful build evidence, but it is not proof of a production
Telegram Mini App. The first proof must compile an actual ImGuiX
Application/Controller/Model slice, not only a Dear ImGui demo, and must
exercise the same backend contract as the HTML client.

No current `imtg` target consumes ImGuiX or Emscripten. No current `imtg` CI
job proves Telegram WebView behavior.

## Proposed integration boundary

```text
ImGuiX/WASM application -> JS bridge -> HTTPS Application API
                                      |
                              shared domain services
```

The bridge is intentionally narrow. It may expose:

- verified launch/session hand-off to the backend;
- Telegram viewport, theme, active-state and BackButton notifications;
- opening a provider/Telegram invoice through the approved surface policy;
- bounded browser storage or clipboard helpers where the application needs them.

It must not expose storage transactions, provider credentials, arbitrary
operator commands, or direct bot FSM/session mutation. Payment completion is
still established by the backend's verified provider event, not a browser
callback or window close.

## PoC gates

The experiment should be a separate ImGuiX/application repository or optional
integration target, not a change to `imtg_core`:

1. build a real C++ screen/controller with Emscripten and the intended shell;
2. use a fake HTTPS API first, then a minimal authenticated test backend;
3. verify launch-data validation, bot/shop scope and idempotent commands;
4. smoke-test WebGL, touch, text input, virtual keyboard, viewport resize,
   DPI, theme, BackButton, suspend/resume and reconnect on Telegram Desktop,
   Android and iOS;
5. measure startup bytes, frame/CPU/GPU use, memory, request latency and
   behavior under a slow or unavailable network;
6. test accessibility and text selection/copy behavior against the HTML
   fallback before choosing a default for commerce forms;
7. document which components are shared semantic view models and which remain
   platform-specific.

Acceptance requires repeatable builds, a bounded resource profile, no leaked
credentials, no direct storage access from WASM, and a recovery story for a
lost command response. A successful desktop WebAssembly demo alone is not
enough.

## Shared UI semantics

The bot and graphical client may share domain data, command names, validation,
localization keys and semantic view models such as `ProductDetails`. They do
not promise identical pixels or controls. Telegram bot output is constrained
to messages, media and buttons; ImGuiX can offer richer cards, tables and
forms. Components that cannot be represented safely on both surfaces remain
platform-specific.

The first shared-state demo should be deliberately small: one product, one
cart quantity and one C++ backend. A quantity changed in the Mini App must be
visible in a subsequent bot query, and a bot change must be visible in the
next Mini App query. No payment or fulfillment is needed for this proof.
