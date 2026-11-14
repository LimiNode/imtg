# Telegram delivery contract

**Semantic owner:** transport/delivery maintainers. **Status:** M2c-c implements
the merged storage-only reference delivery lifecycle. Gateway,
worker orchestration, HTTP, and method-specific retry evidence remain planned for M3.

## Responsibilities

The pipeline has explicit boundaries:

1. **UpdateGateway** parses an inbound update, authenticates the bot boundary, validates size/shape, and normalizes Telegram identifiers.
2. **Conversation scheduler** serializes the derived `ConversationKey`; only stateless token syntax/integrity checks occur before it.
3. **ActionRouter/controller** loads current state inside the serialized boundary, validates expiry, conversation/message/user binding, revision policy and authorization, then transactionally applies the command.
4. **Render-intent store** commits the processed update, state transition, and durable need-to-render marker atomically.
5. **Renderer/Reconciler** consumes the committed intent, loads committed state, compares desired UI with the last applied revision, and plans transport-neutral effects; it does not execute handlers.
6. **Outbox** durably records planned effects in a second transaction and atomically marks or supersedes the render intent. A crash before this transaction leaves the intent eligible for reconciliation again.
7. **DeliveryWorker** executes Telegram HTTP requests, applies rate limits/backoff, and records outcomes with the current lease/fencing token.
8. **Acknowledgement/recovery** updates effect state and schedules review for uncertain results.

`kurlyk` is a recommended future HTTP adapter, not a core dependency. Telegram's external API cannot provide exactly-once delivery without corresponding guarantees, and arbitrary client idempotency keys must not be assumed.

## Gateway and handler modes

The M3 gateway exposes one typed update router with filters, middleware,
deterministic priority and explicit admission outcomes. It may normalize
messages, edited messages, callback queries, chat-member changes, join requests
and payment updates; an application enables only the classes it needs. The
existing per-`ConversationKey` scheduler and bounded queue remain the ownership
boundary. A flood produces an observable overload result (`QueueFull` or
`Closed`) rather than unbounded memory growth.

Handlers are either interactive or headless. Interactive handlers may use
session/FSM state and create a render intent. Headless handlers process
moderation, payment, background or administrative updates without creating a
screen, UI tree or `RenderIntent`. This avoids coupling anti-spam and payment
deadlines to UI work and does not introduce a second generic dispatcher.

Payment pre-checkout is an urgent typed update with an explicit deadline and
priority budget. It must not wait behind ordinary rendering work. The gateway
does not treat a user-supplied payload as payment proof; the commerce module
verifies the provider event and deduplicates the payment identity.

The M2c-b `OutboxEffect` remains the interaction-effect contract during this
migration. Moderation and commerce local state changes are transactional domain
writes, not new `EffectKind` values. A module may add a typed domain outbox or
adapter for its own external operation, but every category must specify its
own idempotency and `UnknownOutcome` proof without changing old logical slots.

Provider-neutral payment identities, webhook authentication, refunds,
`UnknownOutcome` recovery and merchant capability checks are defined in
[PAYMENTS.md](PAYMENTS.md). That contract is separate from Telegram update
authentication and does not make any provider adapter part of `imtg_core`.

## Effect state machine

The concrete states in `OutboxEffectState` are `Pending`,
`ClaimedBeforeDispatch`, `DispatchAdmitted`, `Succeeded`, `RetryableFailure`,
`UnknownOutcome`, `Failed`, and `Superseded`. The earlier design terms
`InFlight` and `Applied` mean the claimed/admitted states and `Succeeded`,
respectively; they are not a second state machine. Desired and confirmed
applied revisions remain separate.

`IOutboxRepository` borrows a read-write transaction for each mutation and
never commits it. Candidate rows are usable only after `Committed`. Every
delivery mutation returning an error rolls the borrowed transaction back;
allocation exceptions propagate and require owner rollback during unwinding.
Instances may be shared, but transactions stay on their owning thread.

The dispatch sequence is strict:

1. Claim an effect in a short transaction and confirm commit.
2. Stage `DispatchAdmitted` in a separate transaction and confirm commit.
3. Only then call the external adapter, outside storage.
4. Record its outcome in a new, fenced transaction.

`mark_dispatch_admitted()` also re-reads the existing render intent, session,
and desired-generation marker, watches those rows, and atomically acquires a
conversation-scoped target reservation. An effect whose session revision is no
longer current is rejected before admission. At most one effect can be
admitted for one `LT1` target.
An expired admitted claim becomes `UnknownOutcome`, not claimable work, and
keeps that reservation. A different effect cannot bypass it using another
intent, token, or effect ID. Confirmed success, proven non-acceptance, or an
explicit terminal/retry/supersession policy releases it atomically with the
effect transition. Explicitly abandoning an uncertain operation accepts the
risk of late remote execution; it does not cancel that operation.

All delivery writes watch authoritative applied-target state. Claims,
admission, renewal, and success reject obsolete desired/applied revisions;
unadmitted obsolete work found by a scan becomes `Failed` without dispatch.
Success atomically writes `Succeeded`, the monotonic applied revision,
last-confirmed effect, and any confirmed physical identity. Delete success
clears the physical mapping. TX2 planning does not advance applied state.
Duplicate success is unchanged only after a fresh read supplies the current
row revision and matching token; an old completion never overwrites new state.

The reference repository scans by stable effect ID with a nonzero page bound,
examination budget, and opaque exclusive continuation. A full pass follows
cursors and then restarts from the beginning. It defaults to eight claims per
effect; lease recovery consumes this budget even if no HTTP call occurred.
Definite non-acceptance at the budget limit, or exhausted unadmitted work,
becomes `Failed`. Unknown work never becomes retryable merely because a budget
or lease expired. Tokens and attempt counters cannot wrap. The durable adapter
will supply the due-index performance model; reference transaction creation
still copies an in-memory snapshot.

`resolve_unknown_success()` is the only success path for an expired or otherwise
`UnknownOutcome` effect. Its typed evidence must name the exact effect, claim
token, observed effect-row revision, observed applied-target row revision, and
LT1 target; the repository verifies the persisted DT1 owner and monotonic
`AppliedTargetState` before atomically
recording `Succeeded`. It never invokes the transport or treats a timeout,
lease expiry, or `DispatchAdmitted` marker as success. A same-revision
independent `Always` intent may update its own confirmed physical mapping, but
an ordinary state-change effect cannot overwrite another effect already
confirmed at that revision.

`retry_unknown_effect(..., safe_repeat_proof, ...)` requires the caller's
explicit assertion of method-specific idempotency or authoritative lookup
evidence. The repository cannot verify external proof. A lost response,
elapsed lease, or an HTTP timeout is not that proof. M3 must validate and audit
it; M2c-c does not invent universal idempotency for Telegram operations.

### Commit and shutdown recovery

`RejectedConflict` and `Failed` publish none of the staged records. Discard the
candidate revision and re-read before retrying. After `Unknown`, do not roll
back, replay a callback, or dispatch from the uncertain result. Read the
authoritative effect and target evidence in a fresh transaction, comparing
the exact immutable slot/payload/version/digest, token, revision and state.
Changed evidence is inconclusive. An observed admitted marker after Unknown
is conservatively uncertain even if the process may have stopped before HTTP.
An unchanged pre-marker row can be admitted only through a new confirmed
transaction while its claim is live; an expired pre-marker row can be reclaimed.
After an uncertain acknowledgement, `Succeeded` and matching applied-target
evidence establish storage publication, not permission for another remote call.

Shutdown of this storage-only slice performs no I/O and does not auto-release
claims. Pending effects remain pending; expired pre-dispatch claims can be
reclaimed; admitted effects preserve uncertainty. Actual intake/drain/callback
coordination is an M3 worker responsibility. Reconstructing repositories over
one in-memory backend tests these logical states, not process-crash durability.

Retrying `sendMessage` after a lost response can create a duplicate because Telegram may have accepted the first request and offers no arbitrary application idempotency key. New-message recovery therefore enters `UnknownOutcome` immediately after response loss; bounded retries apply only to proven non-acceptance or safe-repeat evidence. It must not claim exactly-once. Editing a known message is safer only under a method-specific policy: retry the same edit with the known message identity, tolerate an already-applied result where detectable, and mark `UnknownOutcome` when message existence or content cannot be established. Repeated sends may require an application-level deduplication marker in content or a manual recovery policy, not an invented Telegram guarantee.

### M2c-c failure-injection evidence

The active `CHECK` cases in `tests/outbox_delivery_tests.cpp` cover:

| Boundary | Required reference result |
| --- | --- |
| Two claims or two target admissions from one snapshot | One commit wins; the competing commit conflicts |
| Lease renewal/expiry before admission | Higher-token reclaim; old token cannot admit/acknowledge |
| Crash/stop after the committed marker | Expiry changes to Unknown; the target remains reserved |
| Claim, marker, or acknowledgement commit Failed/Unknown-before/Unknown-after | Fresh reads distinguish publication; no uncertain marker authorizes a fake transport call |
| Storage write failure during marker or any acknowledgement write | Borrowed transaction rolls back; no leaked reservation or partial applied evidence |
| Target revision changes after staging | Watched target rejects commit; obsolete work cannot dispatch |
| Response loss after confirmed admission | Unknown is not re-claimed or blindly retried |
| Duplicate or late success; explicit supersession | Idempotent current row; stale completion cannot replace newer applied evidence |
| Retry budget, token exhaustion, bounded scan | Terminal exhausted work, no wrap, bounded progress |
| Separate bots/chats with the same logical slot | Independent canonical target reservations |
| Stop with pending/unadmitted/admitted work | Pending retained, unadmitted reclaimable, admitted uncertain |

The suite uses a fake call counter at the confirmed admission boundary. It is
not a real transport integration, worker shutdown test, or durability proof.

## Failure example

After transaction 1 commits a session transition and render intent, the process may crash before reconciliation; startup finds the unconsumed intent and safely plans the effect. After the outbox transaction commits, the worker may send `sendMessage` and lose the HTTP response. The effect becomes `UnknownOutcome`, not immediately `Pending` again. Telegram Bot API has no universal operation that reads an arbitrary message by `(chat_id, message_id)`, and a lost send response may not provide a message ID at all. Recovery may use only capabilities confirmed for that method/update, application-visible follow-up evidence, or manual policy; it must not assume generic message lookup. If acceptance cannot be distinguished, the runtime records the uncertainty and avoids unbounded duplicate sends. A later UI revision may supersede desired state, but the technical journal retains the original outcome.

**Verification:** fake transport tests for inbound validation, authorization, send/edit/retry, 429/backoff, uncertain outcomes, revision races, duplicate prevention policy, malformed callbacks, and shutdown with pending outbox records. **Dependencies:** UI/actions, sessions, storage/outbox, and verified official Telegram API limits.
