# Glossary

- **ActionContext** — short-lived capability passed to a UI action callback; dispatches typed events and scoped navigation.
- **Applied UI** — last UI revision known to have been accepted by the delivery boundary.
- **ConversationKey** — explicit identity of one serialized logical interaction: `bot_id + chat_id + session_scope`, where the initial scopes are chat-wide or per-user-in-chat and thread/topic scope is a later transport mapping.
- **Desired UI** — transport-neutral tree produced by a render.
- **Outbox** — durable records for effects that must be delivered after a state transaction commits.
- **Reconciler** — component that compares desired and applied UI and plans idempotent edits.
- **Session** — durable runtime record containing FSM and interaction metadata, not necessarily chat history.
- **ViewContext** — render-scoped read capabilities and UI builder.
- **Typed event** — application message represented by a C++ type and handled independently of transport.
