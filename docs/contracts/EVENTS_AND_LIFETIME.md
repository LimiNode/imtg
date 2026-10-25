# Events and lifetime contract

**Semantic owner:** runtime maintainers. **Status:** contract-only.

## Delivery and ownership

Typed events carry value-owned DTOs or explicitly immutable shared payloads. Synchronous delivery is bounded to the current dispatch scope; deferred delivery transfers ownership into a bounded queue. A subscription handle removes a handler from future selection, but removal is not a join: an already-running callback may still be executing.

`ActionContext::dispatch()` denotes the synchronous transactional command path defined by the storage contract. EventBus publication denotes notification/fan-out and may be asynchronous or non-persistent. A required state transition, processed-update decision, render intent, or delivery request must not depend solely on EventBus delivery.

The event endpoint therefore has three distinct states:

1. **Closed for admission:** no new callbacks are selected.
2. **Quiescing:** in-flight callbacks finish cooperatively or are cancelled at documented cancellation points.
3. **Drained:** the worker has observed zero in-flight callbacks and the owner may destroy handler state.

`event-hub-cpp::EventEndpoint::close()` is treated as the first state only; imtg must not infer that it waits for callbacks. A lifetime guard (for example, a shared cancellation/validity token) prevents a callback from dereferencing an owner that has begun shutdown. The owner remains alive until the runtime reaches `Drained`, or the callback is guaranteed not to access it.

Callbacks may borrow an `ActionContext` or event payload only for the call. Deferred handlers receive owned values. User code may not be forcibly terminated: cancellation is cooperative, and a timeout marks the work failed/abandoned while the worker continues to protect memory safety.

## Shutdown and workers

Shutdown stops intake, closes queues, and rejects new deferred work. A shutdown requested from a worker callback is recorded and signalled to the coordinator; the callback does not synchronously join its own worker. The coordinator drains permitted callbacks, waits for quiescence, cancels queued work, flushes durable journal/outbox state where possible, then destroys subscriptions, workers, and owner objects in that order. If a deadline expires, the process retains an explicit incomplete-shutdown diagnostic; it does not pretend arbitrary user code stopped.

Queues have fixed capacity and a documented backpressure policy: reject low-priority events, coalesce eligible UI refreshes, or apply bounded retry. Event cycles are detected by dispatch depth, event budget, or both. Exceeding the budget stops the chain and records an error rather than spinning indefinitely.

Exceptions are caught at the dispatcher boundary, associated with the event/subscription, and sent to the technical journal. One handler failure does not invoke unrelated handlers unless the event policy says the transaction must abort. Transactional state, render intents, and later outbox effects follow the storage contract.

M1d's `NotificationDispatcher` is the reference implementation of this boundary. It has no `event-hub-cpp` dependency; a future adapter may map the same semantics after review. `ReferenceRuntime` uses a bounded worker pool with per-`ConversationKey` serial mailboxes and bounded queues. `request_shutdown()` closes intake and drains queued work; a callback may request it, but only an external coordinator completes joins.

**Verification:** tests for removal versus in-flight completion, lifetime guards, cancellation points, self-requested shutdown, worker drain/destruction ordering, bounded queues/backpressure, cycle limits, exception isolation, and restart with deferred work. **Dependencies:** event-hub integration review, scheduler, storage/outbox, and technical journal.
