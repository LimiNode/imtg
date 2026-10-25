# Reference scenarios

These are design scenarios, not claims about implemented applications. They deliberately keep business rules outside the imtg core.

## 1. Menu and settings

An update is serialized by `ConversationKey`; a controller reads user/settings services and renders a menu with `ui.button()` actions. A save action dispatches a typed event, commits settings, and schedules a UI revision. **Subsystems:** UI/actions, controller/model, sessions, storage, localization, reconciler, delivery.

## 2. Registration through FSM

The session enters `AwaitingEmail`, validates a message event, transitions to `AwaitingConsent`, and persists each guarded transition. Restart reloads the session and resumes the same state. **Subsystems:** sessions/FSM, users, storage, events, localization.

## 3. Access request with approval, ban, and expiry

A user creates a request; an administrator receives an outbox notification and approves or rejects it. A ban and expiry are checked as guards before each protected action. **Subsystems:** users/chats/history, permissions, FSM, outbox, delivery, jobs.

## 4. Support ticket with optional message retention

The user chooses a category, enters text through an input-wait state, and receives a ticket ID. Message content is stored only when the application opts in with a retention policy and deletion path. **Subsystems:** UI/actions, FSM, users, optional history, storage, localization.

## 5. Monitoring bot

A persistent job polls an external service, emits typed status events, and coalesces notifications per conversation. Delivery retries use the outbox and rate limits; a failure is observable without blocking unrelated sessions. **Subsystems:** jobs, events, outbox, delivery, sessions, technical journal.

## 6. Restart and deferred work recovery

On startup, the runtime loads session revisions and pending outbox/jobs, validates schema versions, and resumes bounded work. Unknown or expired callbacks are rejected safely. **Subsystems:** storage/migrations, sessions, outbox, jobs, callback validation, graceful shutdown.
