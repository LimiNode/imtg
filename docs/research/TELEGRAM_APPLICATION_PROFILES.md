# Telegram application profiles

**Status:** planned application contracts; no production modules are included.
These profiles turn the capability audit into integration acceptance tests while
keeping business policy outside imtg core.

## Shared composition rules

Every profile is assembled from the same boundaries:

- `UpdateGateway` validates Telegram shape, permissions and update identity.
- `UpdateRouter` selects typed handlers through filters and middleware.
- The existing keyed scheduler provides per-chat/per-user ordering and bounded
  admission; it does not create an implicit unbounded work queue.
- A headless handler may execute a domain command without a session, UI tree or
  `RenderIntent`. An interactive handler may use FSM/UI and render intents.
- Domain records have their own identity, revision, retention and transaction;
  they are not hidden in a dialog session.
- Telegram operations are external effects. Local commits and effect planning
  are separate, and a lost response is `UnknownOutcome`, never guessed success.

The first executable Telegram example may cover only a small interactive path.
The profiles below are the target integration gates, not a requirement to land
all application modules before M3 starts.

## `intake-bot` — applications and support tickets

### Reusable requirements

- Multi-step typed fields with required/optional validation and bounded input.
- Durable draft identity scoped independently from the UI session; a user can
  abandon a form and resume after restart.
- Attachment metadata and media references with size/type limits. Binary data
  is stored through an explicit adapter, not in callback payloads or logs.
- Ticket identity, status (`New`, `InProgress`, `Approved`, `Rejected`,
  `Closed`), priority, staff assignment and optimistic transition revision.
- Forum-topic/thread mapping when available, while keeping ticket identity
  independent of Telegram message or callback identity.
- Notifications, approvals and rejections as idempotent planned effects.
- Technical audit and retention/deletion policy separate from user-visible
  history.

### Acceptance scenarios

1. A user abandons a form, restarts the process, and resumes the same draft;
   expired drafts are rejected without reviving deleted attachments.
2. A duplicate update is delivered twice; processed-update identity prevents a
   second ticket or duplicate attachment reservation.
3. Two administrators update one ticket concurrently; one optimistic revision
   wins and the other receives a conflict without a notification for a change
   that did not commit.
4. A notification commit or transport response becomes `Unknown`; recovery
   keeps the ticket state authoritative and does not blindly send a duplicate.
5. A durable backend restart preserves the ticket, assignment, status and
   resumable draft, subject to the configured retention policy.

The ticket module owns field schemas and business transitions. imtg supplies
FSM/session, storage, routing, authorization hooks, localization snapshots and
effect/recovery contracts.

## `shop-bot` — payment and digital fulfillment

### Reusable requirements

- Product/catalog and price snapshots, cart identity, order lifecycle and
  inventory reservation with expected revisions.
- Separate `OrderId`, payment-operation identity and fulfillment identity;
  Telegram callback data is only a routing hint.
- Payment verification based on the provider's authoritative event, not only a
  user-supplied payload. Duplicate payment notifications are idempotent.
- Distinct states: `PaymentPending`, `Paid`, `FulfillmentReserved`,
  `FulfillmentCommitted`, `DeliveryPlanned`, `Delivered`, `DeliveryUnknown`,
  `Refunded`/`Disputed` as applicable. `Paid` never implies `Delivered`.
- An affirmative pre-checkout answer only permits the provider to continue; it
  is not evidence of `Paid`. Only a deduplicated `successful_payment` update
  authorizes the transition to `Paid` and subsequent fulfillment.
- For scarce or single-item inventory, a positive pre-checkout response must
  be protected by a short-lived transactional `InventoryHold` keyed by the
  order/payment attempt. The hold has an explicit expiry, is idempotent, is
  released on decline/cancellation/expiry, and uses the same expected-revision
  or CAS fence as competing orders. If a hold cannot be acquired, the bot
  rejects pre-checkout; it must not promise stock and resolve the race later.
  A successful payment still revalidates the hold before converting it into a
  fulfillment reservation. If the hold is unavailable after an authoritative
  payment, the module records an explicit exception/refund or manual-recovery
  state; it never fabricates successful fulfillment.
- A unique key/file is reserved transactionally so two orders cannot receive
  the same item. Secrets never enter ordinary diagnostics or message text.
- Telegram Stars (`XTR`) handling for digital goods follows current official
  Bot API requirements: answer `pre_checkout_query` within the documented
  deadline, verify `successful_payment`, and only then fulfill. Supported
  third-party providers are an adapter concern for permitted physical goods or
  services; do not infer policy from old shop code.
- Refunds, disputes, coupons, delivery retries, reconciliation and a financial
  audit record are explicit domain operations.

### Transactionally safe flow

```text
Order created
  -> Payment pending
  -> Optional short-lived inventory hold (before positive pre-checkout)
  -> Payment verified
  -> Inventory/fulfillment reserved
  -> Fulfillment committed
  -> Delivery planned
  -> External delivery confirmed / uncertain
```

### Acceptance scenarios

1. Two duplicate payment updates produce one payment receipt and one inventory
   reservation.
2. Two orders race for one unique digital item; one reservation commits and the
   other receives `OutOfStock` without a paid-but-unfulfilled lie.
3. A pre-checkout update is processed on the urgent lane without waiting for a
   normal UI render queue; an expired deadline produces an explicit rejection.
4. Delivery succeeds but its response is lost; the order remains delivered or
   `DeliveryUnknown` according to authoritative evidence and the secret is not
   reissued automatically.
5. A refund or dispute does not mutate the original payment identity and is
   visible in an auditable state transition.
6. Restart during fulfillment resumes from durable reservation/effect state,
   not from a callback closure.

The commerce module owns catalog, orders, provider verification and fulfillment.
M2c-b's effect identity remains compatible; payment and inventory records are
not encoded as new `EffectKind` values.

## `guardian-bot` — moderation and anti-raid

### Reusable requirements

- Configurable text/profanity rules, Unicode normalization/confusable handling,
  invite/link filters and repeated-message detection.
- Per-user and per-chat flood windows with bounded counters and approximate
  aggregation where exact history is unnecessary.
- Join-request handling, CAPTCHA/newcomer trust, temporary restrictions,
  warnings, escalation and administrator overrides.
- Observable raid state machine:

```text
Normal -> Elevated -> Raid -> Lockdown -> Recovery
```

  Thresholds are per-chat configuration based on observable signals (join rate,
  message rate, repetition, links, failed CAPTCHA and newcomer profile), not a
  universal constant. A spike can be legitimate; dry-run and review are first
  class modes.
- Reversible sanctions and evidence: restrictions with expiry, warning history,
  appeal/false-positive path, manual lockdown release and audit of who changed
  policy.
- Bounded work and storage under adversarial traffic: maximum message bytes,
  normalization cost, queue entries, per-chat state, journal retention and
  expensive-analysis rate. An attacker must not exhaust RAM, workers or the
  database by triggering detection.
- AI/LLM analysis, if enabled later, is optional, rate-limited and lower
  authority than explicit administrator rules.

### Acceptance scenarios

1. A configurable flood window moves a chat through `Elevated` and `Raid`, but
   does not lock down on one legitimate burst below the configured confidence.
2. A lockdown applies reversible restrictions, persists expiry and recovers to
   normal after the signal window clears; restart does not lose the state.
3. Duplicate messages and repeated join updates are deduplicated; counters and
   sanctions are not double-applied.
4. A low-confidence profanity or link match enters dry-run/review and does not
   silently delete content without the configured policy.
5. An administrator override wins under revision fencing and is audited; a
   stale worker cannot undo it.
6. A bounded flood exhausts admission capacity and yields an observable
   overload result while unrelated conversations continue to make progress.

The moderation module owns dictionaries, thresholds, evidence policy and
sanctions. The gateway supplies only updates and permission checks; Telegram
administrator rights, privacy mode and join-request visibility limit what can
be observed or changed.

## Capability ownership and milestones

| Capability | Owner | Milestone |
|---|---|---|
| Typed updates, filters, middleware, priorities and backpressure | imtg gateway/runtime contract | M3 |
| Headless handler boundary and urgent update lane | imtg runtime contract | M3 |
| Telegram send/edit/delete/callback effects and UnknownOutcome | transport adapter + M2c-compatible outbox | M3 |
| Locale snapshots and module composition | optional UI/application modules | M4 |
| Forms, drafts, ticket persistence and attachment metadata | `imtg_forms`, `imtg_tickets` optional modules | M4–M5 |
| Permissions, moderation journal, durable jobs/TTL | shared services | M5 |
| Inventory, fulfillment, provider adapters | `imtg_commerce` optional module | M5 |
| Anti-flood/anti-raid policy and guardian reference app | `imtg_moderation` optional module | M5 |
| Webhooks, metrics, dashboards, load tests and raid simulations | operations/extensions | M6 |

No row makes a domain module mandatory imtg core. Each reference application is
an integration acceptance target and may be built in a separate repository or
example tree after the corresponding contracts are approved.

## Dependency-aware order and non-goals

1. Finish M2c-b and the M2c-c delivery acknowledgement boundary without
   changing M2c-b effect identity semantics.
2. Implement the smallest safe Telegram gateway: typed updates, headless
   routing, permission checks, bounded admission and transport recovery.
3. Build one minimal end-to-end example, then add forms/tickets, payment and
   fulfillment contracts, moderation policy and durable jobs.
4. Use `intake-bot`, `shop-bot` and `guardian-bot` as independent integration
   gates; their domain records remain optional modules.
5. Defer AI moderation, advanced dashboards, Mini Apps, analytics and broad CRM
   or inventory integrations until measured demand and a security review exist.

The roadmap must never treat research interest as permission to add speculative
production code, a second event framework, a new dependency or a Telegram
operation whose Bot API guarantees have not been verified.
