# ADR 0001: Immediate UI with an event-driven runtime

- **Status:** accepted as D0 direction
- **Date:** 2026-10-25
- **Owners:** imtg maintainers

## Context

Telegram screens are easiest to reason about when the application describes the desired view on every update, while durable work needs explicit ownership, retries, and restart behavior. A polling-style `if (Button())` API conflates rendering with event handling and encourages transient control flow to become persistence.

## Decision

Use an Immediate-Mode UI tree with callback-based actions and typed events. The canonical public spelling is `snake_case` for every imtg function and method, including `ui.button()`; ImGui inspires the rendering model, not the names. Callbacks are scoped to one action invocation. Durable work is represented by serializable events/commands and outbox records. Controllers may compose views and models without mandatory inheritance. UI, application state, and Telegram delivery remain separate boundaries.

## Consequences

The small-bot path stays concise while complex applications can introduce controllers and services. Stable IDs and deterministic renders become testable. Delivery can retry uncertain Telegram calls without replaying C++ closures. The design requires explicit payload types, callback validation, revision tracking, and more contracts than a synchronous toy API.

## Open questions

1. **Scope of `ConversationKey`:** chat-only, chat-plus-thread, or configurable tuple? Prototype options in M1; this affects locking and storage keys.
2. **Reconciler conflict policy:** last-writer-wins versus revision rejection when Telegram state changes externally? Decide with transport evidence in M3.
3. **Event dispatcher choice:** direct `event-hub-cpp` integration or a narrower façade? Benchmark and lifetime-review before M1/M2 integration.
4. **UI grouping safety:** RAII scopes versus nested callback builders for balanced begin/end calls. Decide before the first public UI implementation.
