# ADR 0001: Immediate UI with an event-driven runtime

- **Status:** accepted as D0 direction
- **Date:** 2026-10-25
- **Owners:** imtg maintainers

## Context

Telegram screens are easiest to reason about when the application describes the desired view on every update, while durable work needs explicit ownership, retries, and restart behavior. A polling-style `if (Button())` API conflates rendering with event handling and encourages transient control flow to become persistence.

## Decision

Use an Immediate-Mode UI tree with callback-based actions and typed events. The canonical public spelling is `snake_case` for every imtg function and method, including `ui.button()`; ImGui inspires the rendering model, not the names. Application composition registers stable, versioned callbacks before update intake. A render references an `ActionHandle` and serializable payload; after restart the registry is rebuilt before an incoming token is resolved. A render pass is not used to discover handlers. Durable work is represented by synchronous transactional commands, durable render intents, and outbox records; asynchronous EventBus notifications cannot carry required state changes alone. Controllers may compose views and models without mandatory inheritance. UI, application state, inbound routing, and Telegram delivery remain separate boundaries.

## Consequences

The small-bot path stays concise while complex applications can introduce controllers and services. Stable IDs and deterministic renders become testable. The render-intent boundary prevents committed state from losing required UI work. Delivery can retry known-message edits while treating lost new-message responses as `UnknownOutcome`, without replaying C++ closures. The design requires explicit payload types, callback validation, revision tracking, deterministic route registration after restart, and more contracts than a synchronous toy API.

## Open questions

1. **Reconciler conflict policy:** last-writer-wins versus revision rejection when Telegram state changes externally? Decide with transport evidence in M3.
2. **Event dispatcher choice:** direct `event-hub-cpp` integration or a narrower façade? Benchmark and lifetime-review before M1/M2 integration.
3. **UI grouping safety:** RAII scopes versus nested callback builders for balanced begin/end calls. Decide before the first public UI implementation.

The minimum `ConversationKey` (`bot_id + chat_id + session_scope`, with `Chat` and `UserInChat` required for M1) is accepted in [SESSIONS_AND_FSM.md](../contracts/SESSIONS_AND_FSM.md). It is no longer an M1 blocker; thread/topic mapping remains a M3 transport decision.
