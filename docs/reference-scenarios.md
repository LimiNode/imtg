# Reference scenarios

These are design scenarios, not claims about implemented applications. They deliberately keep business rules outside the imtg core.

## 1. Menu callback changes persistent session state

**Flow:**

1. A `Chat`-scoped `ConversationKey` is loaded at revision 4.
2. The controller renders `settings.v1` and registers a `toggle_notifications.v1` route with a serializable boolean payload.
3. The renderer emits a signed callback token; Telegram later returns it in a callback update.
4. `UpdateGateway` validates shape/signature and `ActionRouter` checks chat/user/message binding, expiry, and revision policy.
5. The route handler is located in the registry, receives an action-scoped context, and dispatches `NotificationsToggled`.
6. One `UnitOfWork` records the processed update, saves the session at expected revision 4 → 5, and appends a UI/outbox effect.
7. The new state renders a desired revision; the reconciler plans an edit and the delivery worker records its outcome.

**Failure path:** a duplicate update is rejected by processed-update storage; a stale token is recorded as a rejected action and does not mutate state; a revision conflict rolls back the transition and asks for a fresh render. If delivery is uncertain, state remains committed and the effect enters `UnknownOutcome` rather than replaying the C++ callback.

**Subsystems:** UI/actions, sessions/FSM, storage, events, reconciler, outbox, delivery, and localization.

## 2. Administrator approval with durable recovery

**Flow:**

1. A user submits an access request; the FSM stores the request state and a typed `AccessRequested` event.
2. The transaction commits processed update, state transition, request record, and an outbox notification addressed to an administrator.
3. The administrator's action token resolves through a versioned `approve_access.v1` route and authorization service checks the administrator role and request revision.
4. Approval commits the access grant, expiry timestamp, audit event, and a confirmation outbox effect. A scheduled job later evaluates expiry and emits a revocation event.
5. Delivery workers retry effects independently of the FSM; restart reloads pending jobs/outbox rows and continues from their explicit states.

**Failure path:** if the approval token targets a deleted/expired request, the route fails closed and writes a technical journal record. If the notification send response is lost, it becomes `UnknownOutcome`; recovery does not blindly send duplicates. If approval commit conflicts with a simultaneous ban, the transaction returns a typed conflict and neither grant nor outbox effect is committed.

**Subsystems:** users/chats/permissions, sessions/FSM, storage, technical journal, jobs, outbox, delivery, and localization.

## 3. Registration through FSM

The session enters `AwaitingEmail`, validates a message event, transitions to `AwaitingConsent`, and persists each guarded transition. Restart reloads the session and resumes the same state. Invalid input produces a bounded validation response without changing the state. **Subsystems:** sessions/FSM, users, storage, events, localization.

## 4. Support ticket with optional message retention

The user chooses a category, enters text through an input-wait state, and receives a ticket ID. Message content is stored only when the application opts in with a retention policy and deletion path; technical events remain separate. **Subsystems:** UI/actions, FSM, users, optional history, storage, localization.

## 5. Monitoring bot

A persistent job polls an external service, emits typed status events, and coalesces notifications per conversation. Delivery retries use the outbox and rate limits; a failure is observable without blocking unrelated sessions. **Subsystems:** jobs, events, outbox, delivery, sessions, technical journal.

## 6. Restart and deferred-work recovery

On startup, the runtime loads session revisions and pending outbox/jobs, validates schema versions, and resumes bounded work. Unknown or expired callbacks are rejected safely. **Subsystems:** storage/migrations, sessions, outbox, jobs, callback validation, and graceful shutdown.
