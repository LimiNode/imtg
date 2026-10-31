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

Only `Completed` and `Superseded` are terminal for intent planning. `Planned`
means that TX2 durably published (or verified) the required effect; it is not a
delivery acknowledgement and may still have an active or unresolved outbox
effect. A later reconciliation may supersede a `Planned` intent only through a
new fenced transaction that preserves the already-published effect and records
the supersession reason.

### Compatibility with M2b records

M2b records contain `id`, `conversation`, `session_revision`, `metadata`, and
an optional `origin`; they contain no claim, lease, state, or effect fields.
M2c reads every valid M2b record as a `Pending` intent with no active claim and
with `next_eligible_at` equal to the backend's current eligible time. The M2b
identity is unchanged: the coalescing form remains
`conversation|r=session_revision`, while an `Always` origin remains part of the
identity. An M2c claim/revision envelope is additive metadata, not a rewrite of
the identity.

Backends may materialize the additive fields in a versioned envelope or in a
side table keyed by `RenderIntentId`, but must use one schema gate and one
transaction when publishing the representation. An old record is migrated to
the additive representation only after it has been read and validated; a
malformed or unknown version is `SchemaError`/`RecoveryRequired`, never an
implicitly claimable row. The session repository and TX1 encoding remain
unchanged. MDBX migration must preserve the original bytes/identity and be
restartable before serving new claims.

The desired-generation marker is likewise additive. For existing M2b records,
the first M2c admission uses a schema/admission gate and one compare-and-set
transaction to atomically initialize a deterministic baseline generation. The
initialization may not lower an already-created generation; a concurrent
initializer or first M2c TX1 either observes the existing value or retries its
CAS. No claim or effect is published until that gate has committed. Every
subsequent M2c TX1 that creates or coalesces an intent advances the shared
marker in the same transaction as the intent write. Thus an old M2b identity
remains valid, while new TX2 instances can watch one monotonically versioned
head rather than relying on a non-serializable absence query. The same schema
gate covers legacy-envelope validation, so concurrent TX1/TX2 operations cannot
observe a partially initialized marker.

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

The same bounded-scan rules apply to outbox effects, but their recovery state is
different from render intents. A backend may return fewer rows than the limit
when the page ends, but it must not claim more than the configured global and
per-worker budgets. Claiming is atomic with the fencing-token increment. If the
transaction result is `Unknown`, recovery must inspect the persisted row before
attempting another claim; it must not assume that the increment did or did not
commit.

The eligible ordering is `(next_eligible_at, stable_record_id)` with an
exclusive continuation key. If claiming changes `next_eligible_at`, the worker
continues from the returned key and then starts a fresh page; it never reuses a
mutable numeric offset. A bounded implementation must document its fairness
rule (round-robin continuation or periodic restart from the first key) so a
hot prefix cannot starve later records. Every claim attempt advances the token
atomically and every completion validates the token and expected row revision.

## Reconciliation and TX2

1. The worker claims an intent and loads the current committed session/model
   revision. It does not trust a snapshot embedded in the intent.
2. It renders the desired transport-neutral tree and compares the desired
   revision with the applied revision known by the target adapter.
3. If the desired state is already applied, TX2 completes the intent without a
   new effect, using the claim token and expected intent revision.
4. If a delivery operation is required, TX2 writes exactly one stable
   `OutboxEffect` and marks the intent `Planned` or `Completed` according to
   the operation contract. The insertion (or byte-identical verification),
   claim-token and expected-revision checks, desired-revision validation, and
   intent transition all use the same caller-owned `ITransaction`/`UnitOfWork`.
   They either all publish or none publish. A backend may expose an equivalent
   atomic operation, but it must document that it has the same boundary.
5. If a newer intent supersedes this one before TX2, the worker records
   `Superseded` with the newer identity. It does not erase an effect that was
   already committed for the older revision; the delivery worker applies the
   documented coalescing policy.

TX2 may be retried after a definite rollback. A `RejectedConflict` or
`StaleClaim` causes the worker to discard its local plan and rescan. `Failed`
and `Unknown` commit outcomes are not necessarily persistable in the same
backend: the runtime emits an observable diagnostic, may append a best-effort
technical journal entry, and performs authoritative recovery inspection in a
new transaction. It must not claim that a durable diagnostic exists when the
backend could not commit it, and neither outcome is silently treated as a
successful completion.

Claim and delivery acknowledgement may use additional short storage
transactions. "TX1" and "TX2" name the two principal business transactions;
they do not prohibit these lifecycle transactions or read-only recovery
inspections.

### Durable pre-dispatch boundary

The effect worker must establish a durable conservative boundary before it
invokes any transport. The required sequence is:

```text
TX: claim effect
  -> commit
TX: record MayHaveDispatched / DispatchAdmitted marker
  -> commit confirmed
call transport asynchronously
  -> admission accepted or rejected
  -> completion callback
  -> fenced outcome transaction
```

`MayHaveDispatched` (also called `DispatchAdmitted` in the state machine) is a
durable effect-row marker, not an in-memory boolean. It means that the external
operation may have started, even if the transport has not yet acknowledged it.
The marker is committed in a separate short transaction before the transport
call. A worker must not dispatch from an uncommitted marker. If that marker
transaction returns `Unknown`, the worker performs authoritative inspection of
the effect row in a new transaction and does not dispatch merely because the
commit was not confirmed. An observed committed marker is conservatively
recovered as `UnknownOutcome` after a crash before the HTTP call; an absent
marker may be retried only after the storage record proves that no marker was
published and the claim is still valid.

Only a synchronous admission result that proves the external request was not
accepted may return the effect to `Pending`/`RetryableFailure`. A response loss,
process crash, callback loss, or ambiguous transport error after the marker is
committed is `UnknownOutcome` until an idempotency or lookup proof resolves it.
This logical protocol does not claim physical durability: the in-memory
backend models commit outcomes, while MDBX must later demonstrate persistence
across process interruption.

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

`UnknownOutcome` is not an invitation to blindly resend. The durable boundary
is the committed `MayHaveDispatched`/`DispatchAdmitted` marker: before it
becomes true, a definite rollback or
definitely-not-started result may return an effect to `Pending`; once external
dispatch has started, a lost response records `UnknownOutcome` and lease expiry
alone never returns it to `Pending`. Recovery may retry only when the
transport/method contract provides a safe idempotency or lookup proof.
Otherwise it remains visible for reconciliation/manual policy, while a newer
desired revision may supersede future work. Fencing protects storage
acknowledgement, not external side-effect deduplication.

The separate effect state machine is therefore:

```text
Pending -> ClaimedBeforeDispatch -> DispatchAdmitted
                         |                  |
                         |                  +--> Succeeded / RetryableFailure
                         |                  +--> UnknownOutcome
                         +--> Pending only after a definite pre-dispatch rollback
UnknownOutcome -> retry only under an explicit safe-repeat proof
```

An expired `ClaimedBeforeDispatch` can be reclaimed only after storage proves
that dispatch was not admitted. An expired `DispatchAdmitted` claim is
recovered as `UnknownOutcome` (or remains `InFlight` until an explicit
transport reconciliation), not blindly resent.

### Applied-revision authority

The authoritative applied state is a durable `AppliedTargetState` record keyed
by a stable target identity, for example `(conversation, target_message_id,
target_slot)`. The key is part of the target identity; separate Telegram
messages or UI positions never collapse into one revision. The record contains
the last confirmed `applied_revision`, the last confirmed effect identity, and
the storage revision/fence that admitted that observation.

TX2 planning never advances `applied_revision`. Only a confirmed external
result may update it, in a short transaction that validates the effect's active
claim token, expected effect revision, target identity, and monotonic revision
rule. A late callback with a stale token or older revision returns `StaleClaim`
and cannot overwrite newer applied state. `UnknownOutcome` leaves the applied
revision unchanged and distinguishable from `Succeeded`.

On restart, reconciliation reads desired intent, outbox state, and
`AppliedTargetState` from storage; it never reconstructs applied state from the
in-memory `FakeReconciler` or an unpersisted callback.

### Stable OutboxEffect identity and supersession

The stable logical effect slot is separate from the operation payload:

```text
LogicalEffectSlot = (
 render_intent_id,
 target_identity,
 effect_kind,
 effect_position)
```

`effect_position` is a stable slot/discriminator for operations that share a
target (for example a message edit versus a keyboard replacement). The
`OutboxEffectId` is stable for this logical slot; canonical payload bytes and a
versioned payload digest are stored in the effect record, but are not part of
the slot identity. A repeat of the same slot with byte-identical payload is an
idempotent verification. A repeat of the same slot with different payload is a
conflict and must fail the atomic unique-slot/CAS check; it must not create a
second effect or overwrite the first one. An `Always` intent has a distinct
`render_intent_id`, so independent requests at the same session revision retain
independent slots.

Before TX2 commits, the worker rechecks the latest desired intent/revision in
the same transaction. TX2 must watch/write the following dependencies under
the optimistic/snapshot transaction model:

- session revision;
- applied target revision;
- current render-intent claim token and row revision;
- the latest desired head or a stable desired-generation marker; and
- the conflicting outbox logical-slot identity.

If checking for a newer intent is required, TX1 atomically advances a shared,
versioned desired-generation marker together with the new intent. TX2 watches
that marker. A newer intent committed after the TX2 snapshot therefore causes
TX2 to fail with `RejectedConflict`/retry rather than publishing stale output.
Independent `Always` intents at the same session revision do not supersede one
another merely because they share a session revision.

Deterministic interleaving:

```text
TX2 snapshot reads desired generation G5
newer TX1 commits generation G6
TX2 commit must fail because its watched generation changed
worker discards the stale plan and rescans
```

If the desired-generation watch fails, the TX2 commit is rejected and no
mutation from that transaction is published: it cannot simultaneously mark
the old intent `Superseded`. The worker discards the rejected transaction,
starts a new transaction, re-reads the authoritative desired state, and only
then may mark the old intent `Superseded` with the newer identity. Any already-
published old effect is retained. Supersession is never based solely on a
pre-render snapshot.

### Target ordering and external limitations

Target identity is logical and stable before a physical Telegram
`message_id` exists. After a successful send, the durable mapping from logical
target to physical `message_id` is recorded with the confirmed effect. The
storage protocol prevents concurrent conflicting dispatches for one logical
target by fencing the target row and its logical slots. An old in-flight or
`UnknownOutcome` effect is not silently discarded when a newer revision
appears: policy must choose lookup, safe idempotent retry, conservative manual
reconciliation, or explicit supersession while retaining the audit record.

Late completion and late remote application are expected. Fencing rejects a
stale storage callback, and monotonic applied-revision checks prevent an older
confirmed observation from overwriting a newer one. Storage fencing cannot
guarantee strict external execution order: a remote server may apply two edits
in reverse order, and a server without conditional writes may apply stale
content. kurlyk sequential rate limits can provide pacing only; they are not
durable ownership or ordering. The implementation must document this
conservative limitation and use lookup/idempotency or target-level policy when
strict remote ordering is required.

## Failure-injection matrix

The M2c conformance suite must exercise each boundary:

| Boundary | Required durable result |
|---|---|
| Before TX1 commit | No processed marker, state transition, or render intent is visible |
| After TX1 commit, before claim | Pending intent is found by restart scan |
| Claim transaction `Unknown` | Recovery inspects the intent/token in a new transaction before another claim |
| After claim, before render | Lease expiry makes it claimable; old token is stale |
| During render | No outbox effect is visible; intent remains eligible after lease expiry |
| Newer session revision while rendering | TX2 rechecks desired state and either plans the current effect or records fenced supersession |
| Before TX2 commit | No effect and no terminal intent update are visible |
| TX2 conflict/stale claim | Existing newer record wins; old completion is rejected |
| TX2 `Unknown` | Recovery inspects both intent and effect; it does not guess publication |
| Competing TX2 planners | Exactly one byte-identical effect identity wins; incompatible payload is a conflict |
| Crash before pre-dispatch marker commit | No transport call is inferred; after authoritative inspection the still-unmarked claim may return to `Pending` |
| Pre-dispatch marker commit `Unknown` | Inspect the effect row before dispatch; never dispatch solely because commit confirmation was lost |
| Crash after marker commit, before HTTP | Recover conservatively as `UnknownOutcome`; do not assume that no request was sent |
| Transport admission rejection | Return to `Pending`/`RetryableFailure` only when the adapter proves the request was not accepted |
| Successful submission with lost response | Preserve `UnknownOutcome` until an idempotency key or authoritative lookup resolves it |
| Two conflicting payloads for one logical slot | Atomic unique-slot/CAS rejects the second payload; at most one effect row exists |
| Newer session revision during TX2 | Watched desired generation causes a conflict/retry; stale output is not published |
| Independent `Always` intents at one revision | Distinct origin/intent/effect identities remain independently processable |
| Two edits of one target complete in reverse order | Stale token/revision cannot overwrite the newer applied state; remote ordering limitation is recorded |
| Unknown older edit plus newer desired revision | Retain the unknown effect and reconcile the newer revision under explicit lookup/idempotency policy |
| Initial send before a physical message ID exists | Use a stable logical target; record the logical-to-physical mapping only after confirmed send |
| After TX2 commit, before delivery | Outbox effect is found by restart scan |
| Effect committed but worker not started | Effect remains `Pending` and is claimable by a later worker |
| External dispatch admitted, response lost | Effect becomes `UnknownOutcome`; lease expiry does not make it `Pending` |
| External request definite failure | Bounded retry or terminal failure is recorded |
| Lease expiry before external dispatch | Reclaim only after durable proof of no dispatch admission |
| Lease expiry after external dispatch | Preserve `UnknownOutcome`/in-flight uncertainty; never blindly resend |
| Delivery claim expiry | A higher fencing token may recover; stale acknowledgement is rejected |
| Duplicate effect identity with conflicting payload | TX2 rejects the conflict and preserves the first durable effect |
| Restart without the original callback | Reconstruct from persisted session, intent, outbox and applied-target records; never execute the callback |
| Shutdown with pending work | Intake stops; leases/effects remain recoverable; no new callback starts after drain |

Every case runs with bounded page sizes, two competing workers, a revision
conflict, and a restart reconstruction pass. In-memory tests prove logical
ordering and fencing only. The in-memory restart simulation reconstructs a new
worker/backend view from committed records; it does not simulate a physical
process crash. MDBX tests later add process interruption and physical
durability evidence.

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

