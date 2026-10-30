# ADR 0005: M2c render-intent reconciliation and transactional outbox

**Status:** Proposed  
**Scope:** backend-neutral protocol design; no delivery transport or MDBX implementation  
**Depends on:** M2a storage transactions and M2b typed sessions/FSM

## Context

M2b atomically records a processed update, a session transition, and a minimal
render intent. That commit deliberately stops before rendering or making a
Telegram call. The process can stop after TX1, while rendering, while planning
TX2, after TX2, or after an external request whose response is lost. The next
milestone must make each boundary recoverable without treating an in-process
queue as durable state.

The in-memory backend can validate this logical protocol and failure matrix. It
cannot prove that data survives a process or machine crash. Physical durability
and MDBX transaction mapping remain M2d/M2e work.

## Decision

M2c keeps two storage transactions and makes each work item explicitly owned by
stable identity, lease, and fencing state:

```text
TX1:
  ProcessedUpdate + Session/FSM transition + RenderIntent -> commit

post-commit:
  claim RenderIntent -> load current state -> render desired UI
  -> reconcile against applied revision

TX2:
  OutboxEffect + complete/supersede RenderIntent -> commit

delivery:
  claim OutboxEffect -> external attempt -> fenced outcome -> recovery policy
```

TX2 is the only place that publishes a delivery effect. A renderer may render
or retry planning many times, but it must not call Telegram as an implicit part
of TX1 and must not delete an intent before TX2 commits.

## Stable identities and revisions

- `RenderIntentId` remains stable for the logical conversation and TX1 event.
  It is not a worker-attempt id.
- `OutboxEffectId` is stable for one externally observable operation. Repeating
  reconciliation for the same desired/applied revision reuses this identity or
  records an explicit supersession; it does not silently append duplicates.
- `ClaimToken` is opaque, monotonically fenced storage state. A new claim for a
  row is greater than every prior claim for that row, including after lease
  expiry or worker restart.
- `desired_revision` is the revision the render intent asks the reconciler to
  represent. `applied_revision` is the last revision known to be represented by
  the target message/effect. They are compared by value, never by wall-clock
  time.
- A claim lease is an eligibility guard, not ownership that survives expiry.
  Completion, supersession, lease renewal and acknowledgement require the
  currently active token and expected row revision.

No callback, pointer, closure, process-local address, or worker thread id is
serialized into these records.

## Render-intent state machine

The backend-neutral record has the following semantic states:

| State | Meaning | Recovery |
|---|---|---|
| `Pending` | TX1 published work not yet claimed, or a claim lease expired | Eligible for bounded claim scan |
| `Claimed` | A worker has a live token and lease | Renew only before expiry; stale completion is rejected |
| `Planned` | TX2 has published an outbox effect for the desired revision | Intent may be completed or superseded atomically with effect planning |
| `Completed` | The intent is terminal for its desired revision | Retain audit metadata according to retention policy |
| `Superseded` | A newer desired revision makes this intent unnecessary | Never remove the newer intent/effect; retain the reason and fence |

`Planned` is a storage fact, not evidence that Telegram accepted the effect.
The outbox row carries the external attempt state separately.

## Claims, leases, and bounded scans

The planned repository operations are:

```text
claim_render_intents(limit, now, lease_duration) -> [(intent, ClaimToken, lease)]
renew_render_intent(id, token, expected_revision, lease_until)
complete_render_intent(id, token, expected_revision)
supersede_render_intent(id, token, expected_revision, newer_intent_id)
```

Claims select only rows whose `next_eligible_at <= now` and whose lease is
absent or expired. The query has a deterministic ordered key, a hard non-zero
limit, and a continuation key (or equivalent stable cursor). A worker never
loads the whole table to find work. The continuation key is returned with the
page and is exclusive on the next scan; it is not a mutable offset.

The same rules apply to outbox effects. A backend may return fewer rows than
the limit when the page ends, but it must not claim more than the configured
global and per-worker budgets. Claiming is atomic with the fencing-token
increment. If the transaction result is `Unknown`, recovery must inspect the
persisted row before attempting another claim; it must not assume that the
increment did or did not commit.

## Reconciliation and TX2

1. The worker claims an intent and loads the current committed session/model
   revision. It does not trust a snapshot embedded in the intent.
2. It renders the desired transport-neutral tree and compares the desired
   revision with the applied revision known by the target adapter.
3. If the desired state is already applied, TX2 completes the intent without a
   new effect, using the claim token and expected intent revision.
4. If a delivery operation is required, TX2 writes exactly one stable
   `OutboxEffect` and marks the intent `Planned` or `Completed` according to
   the operation contract. Both changes commit atomically.
5. If a newer intent supersedes this one before TX2, the worker records
   `Superseded` with the newer identity. It does not erase an effect that was
   already committed for the older revision; the delivery worker applies the
   documented coalescing policy.

TX2 may be retried after a definite rollback. A `RejectedConflict` or
`StaleClaim` causes the worker to discard its local plan and rescan. `Failed`
and `Unknown` commit outcomes are persisted as technical diagnostics and
recovered according to the backend contract; neither is silently treated as a
successful completion.

## Outbox effect state and unknown outcomes

An effect has a stable identity, target operation, desired/applied revision,
attempt count, `next_eligible_at`, claim token/lease, and terminal status. The
initial M2c protocol distinguishes at least:

- `Pending` — no external attempt is currently active;
- `InFlight` — one worker owns a live claim;
- `RetryableFailure` — the adapter proved the operation was not accepted and
  supplied a bounded retry time;
- `UnknownOutcome` — the request may have been accepted but the result was not
  established; and
- `Succeeded`, `Failed`, or `Superseded` — terminal policy decisions.

`UnknownOutcome` is not an invitation to blindly resend. Recovery may retry
only when the transport/method contract provides a safe idempotency or lookup
proof. Otherwise it remains visible for reconciliation/manual policy, while a
newer desired revision may supersede future work. A stale worker cannot turn an
unknown row back into `Pending` because its token no longer matches.

## Failure-injection matrix

The M2c conformance suite must exercise each boundary:

| Boundary | Required durable result |
|---|---|
| Before TX1 commit | No processed marker, state transition, or render intent is visible |
| After TX1 commit, before claim | Pending intent is found by restart scan |
| After claim, before render | Lease expiry makes it claimable; old token is stale |
| During render | No outbox effect is visible; intent remains eligible after lease expiry |
| Before TX2 commit | No effect and no terminal intent update are visible |
| TX2 conflict/stale claim | Existing newer record wins; old completion is rejected |
| TX2 `Unknown` | Recovery reads the row and does not guess publication |
| After TX2 commit, before delivery | Outbox effect is found by restart scan |
| External request definite failure | Bounded retry or terminal failure is recorded |
| External request response lost | `UnknownOutcome`, never an implicit successful completion |
| Delivery claim expiry | A higher fencing token may recover; stale acknowledgement is rejected |
| Shutdown with pending work | Intake stops; leases/effects remain recoverable; no new callback starts after drain |

Every case runs with bounded page sizes, two competing workers, a revision
conflict, and a restart reconstruction pass. In-memory tests prove logical
ordering and fencing only; MDBX tests later add process interruption and
physical durability evidence.

## Explicit non-goals

- No Telegram HTTP calls, Bot API DTOs, retry headers, or credentials in M2c.
- No MDBX types, DBI creation, migrations, or durability claims in this design.
- No per-user or per-chat database. The future MDBX profile remains the six
  logical DBIs defined by the storage contract.
- No generic executor implementation in imtg. The event-hub capability audit
  tracks upstream bounded TaskManager, injectable clock, and keyed executor
  work separately.

## Acceptance and follow-up

M2c design is accepted when the backend-neutral interfaces, state transitions,
failure matrix, and restart rules have an independent review. Implementation
then proceeds in two isolated slices:

1. extend the in-memory repositories and conformance suite with claims,
   fencing, reconciliation and TX2; and
2. implement an optional MDBX adapter only after the same suite passes and the
   transaction-result/physical durability mapping is reviewed.

The design is linked from [M2_PLAN.md](../M2_PLAN.md) and
[STORAGE.md](../contracts/STORAGE.md); those documents remain the owners of
milestone status and backend-neutral storage vocabulary.

