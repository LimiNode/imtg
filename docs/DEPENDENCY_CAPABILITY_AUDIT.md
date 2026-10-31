# Dependency capability audit

**Status:** proposed research baseline for M2c/M2d/M3 planning.  
**Audit date:** 2026-10-31.  
**Scope:** source review only; this document does not add, pin, or integrate a dependency.

This audit records which generic capabilities already exist in the LimiNode
libraries considered for imtg. It is deliberately narrower than a dependency
selection: an implementation PR must still pin the reviewed source, validate
its installed-package surface, and prove the imtg adapter against conformance
tests and failure injection.

## Decision vocabulary

- **Reuse as-is** — the existing public behavior matches the required generic
  capability. imtg may compose it without maintaining a competing primitive.
- **Adapter** — reuse the implementation behind an imtg-owned boundary that
  translates types, failure states, configuration, and lifetime rules.
- **Extend upstream** — the capability is generic and belongs in the dependency,
  but the audited revision does not provide the required contract.
- **Implement in imtg** — the behavior is specific to imtg persistence,
  reconciliation, Telegram semantics, or policy.
- **Defer** — no current milestone should take the dependency or expose the
  capability merely because it is available.

## Audited revisions

| Repository | Exact source SHA | Role examined |
|---|---|---|
| `LimiNode/event-hub-cpp` | `2d4b09763603db2e5f9c5ca5bce7de3ffc9332b0` | Events, passive tasks, run loops, module lifecycle, calendar scheduling |
| `LimiNode/kurlyk` | `b62cd53f672c47b74bb14d762ea9dc59a9df19ce` | HTTP admission, cancellation, retries, rate limits, callback completion |
| `LimiNode/time-shield-cpp` | `d5f1203f24341e4146af35d5bb36e762056d181f` | Monotonic timers, deadlines, scheduling and shutdown |
| `LimiNode/mdbx-containers` | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` | MDBX transactions, ordered tables, composite keys, sequences and replication outbox |
| `LimiNode/optionx_cpp` | `982413438755038982d990f251c77ecb09f2e294` | Reference Telegram signal bridge lifetime and callback patterns |

The repositories were inspected independently at these commits. Their source
trees under a local ignored build directory are audit material, not candidate
submodules.

## Capability matrix

| Capability | Decision | Existing evidence | imtg fit or remaining gap |
|---|---|---|---|
| Typed direct/queued events | Adapter | `event_hub::EventBus` supports direct and queued delivery, snapshot draining, subscriptions, exception reporting, notifier integration and pending counts. | Suitable for optional notifications. It must not carry required transactional transitions or durable effects. |
| Subscription lifetime and request/result | Adapter | `EventEndpoint` owns a guard and RAII subscriptions; `EventAwaiter` and endpoint request helpers provide predicates, cancellation, timeouts, callbacks and futures. | Awaiter timeout/cancellation is cooperative: it advances only at accepted `emit_direct()`/`process()` poll points. imtg still owns staged shutdown and quiescence proof. |
| Passive delayed/periodic work | Extend upstream | `TaskManager` is multi-producer/single-consumer, owns no thread, supports delayed/periodic tasks, cancellation, futures, priorities, exception handling and a notifier. | The audited queue has no configurable admission limit and reads `steady_clock::now()` directly. Do not use it for durable jobs. |
| Bounded `TaskManager` admission | Extend upstream | `pending_count()` is observable, but submissions have no capacity contract or typed full/closed result. | Add configurable capacity and explicit admission results in `event-hub-cpp`, preserving existing APIs and passive processing. |
| Deterministic `TaskManager` time | Extend upstream | Delayed and periodic paths use `TaskManager::Clock = std::chrono::steady_clock` and call `Clock::now()` directly. | Inject a monotonic now-provider upstream. Do not add a second wall-clock provider to `CalendarScheduler`, which already has one. |
| Run-loop and module lifecycle | Reuse as-is | `RunLoop` processes non-owning buses/managers on the caller thread. `ModuleHub` owns its bus/modules and supports passive `process()`, blocking `run()`, background `start()` and module-private workers. | Select one ownership mode explicitly. An adapter must preserve stop ordering and must not infer callback quiescence merely from endpoint closure. |
| Calendar rules | Reuse as-is | `CalendarScheduler` already exposes `now_provider`, `last_due_utc_ms`, missed-run policy and overlap policy. | Suitable for calendar calculation/scheduling, not persistent job recovery by itself. Avoid duplicating these controls in imtg. |
| Generic keyed serial worker pool | Extend upstream | No audited `ModuleHub`/`TaskManager` abstraction provides bounded multi-worker admission plus per-key FIFO and no same-key overlap. | A generic `KeyedSerialExecutor` belongs upstream. The existing imtg `ReferenceRuntime` remains the accepted reference and is not replaced without semantic comparison and measurements. |
| HTTP queue and per-client admission | Adapter | `kurlyk::HttpRequestManager` has `max_pending_requests` and typed `SubmitResult`; `HttpClient` has group-scoped `max_in_flight` and an outstanding count. | Reuse behind an imtg transport adapter. Configure non-zero bounds; default zero means unbounded. |
| HTTP cancellation and quiescence | Adapter | Request/group cancellation, `wait_requests()`, timed wait and cancellable group waiters exist. `HttpClient` destruction cancels its group and waits for final callbacks. | The adapter owns clients long enough for callbacks to finish and must not block-wait from the network worker thread. Shutdown must first stop imtg intake, then cancel/drain transport work. |
| Generic retry and rate limiting | Adapter | `kurlyk` supplies retry attempts/status classification, shared rate-limit handles, general/specific keyed limits and sequential in-flight tokens. | Reuse the mechanics. Telegram `Retry-After`, Bot API method/chat policy, durable retry state and delivery uncertainty remain imtg concerns. |
| Telegram delivery policy and unknown outcome | Implement in imtg | `kurlyk` reports HTTP/client results but does not own imtg outbox identity or Telegram reconciliation. | Persist attempts and `UnknownOutcome`; never automatically retry a send whose acceptance cannot be distinguished. |
| Monotonic deadline/elapsed helpers | Defer | `time-shield::DeadlineTimer` and `ElapsedTimer` wrap `steady_clock` and also accept caller-provided time points for deterministic queries. | Useful utility types, but not a reason to add a core dependency. Both are single-thread-use objects. |
| In-process timer service | Defer | `TimerScheduler` supports manual processing or one owned worker, fixed-rate timers, single-shot helpers and `stop_and_wait()`. | Its queue is unbounded, cancelled entries are removed lazily, clock reads are direct and callback exceptions are swallowed. Prefer the selected event-hub path unless a separate timer service has a measured use. |
| MDBX transaction and table primitives | Adapter | `mdbx-containers` provides RAII read/write transactions, ordered tables/cursors, early-stop range callbacks, typed canonical composite keys and a per-DBI transactional `TableSequence`. | Build an optional M2d backend. Keep MDBX types out of public core headers and keep every live transaction/cursor on its owning thread. |
| Bounded ordered scans | Adapter | Key tables expose lower/upper bounds and streaming `for_each_range`; cursor implementations use `MDBX_SET_RANGE`/`MDBX_NEXT`. `LogicalOutboxStore` demonstrates ordered prefix scanning with a result limit. | The imtg backend must define stable continuation keys and a non-zero page limit; do not materialize an unbounded range. |
| Durable local sequence allocation | Adapter | `TableSequence::reserve()` allocates a contiguous per-DBI range inside the caller's write transaction and rolls it back with that transaction. | Suitable for local fenced counters when the schema chooses a stable DBI. It is not a global cross-database or replicated identity source. |
| `mdbx-containers` logical outbox | Defer | `LogicalOutboxStore` atomically queues ordered logical replication envelopes, persists receiver acknowledgement frontiers and scans pending prefixes. | This is a replication outbox with destination/origin/receiver semantics, not the imtg Telegram effect outbox. Reuse its patterns and MDBX primitives, not its record model. |
| Bot API transport from `optionx_cpp` | Defer | `TelegramSignalBridge` consumes a normalized message source backed by `tg-client-stdio`; its design uses Telethon user authorization and MTProto outside C++. | It is not a Telegram Bot API HTTP adapter and must not become an imtg runtime dependency for transport. |
| Signal-bridge lifecycle patterns | Defer | The bridge serializes intake, captures shared runtime state in callbacks, reserves dedupe/state before dispatch, classifies a throwing dispatch callback as ambiguous, and joins or defers joining its watchdog safely. | Reuse the reasoning in imtg tests/design: fail-closed reservation, explicit ambiguous outcome, shared lifetime state and no self-join. Do not copy its trading domain policy or add an OptionX dependency. |

## Findings by dependency

### event-hub-cpp

`TaskManager` is a passive scheduler. Producer-facing submission, cancellation
and count queries are thread-safe; one application-owned consumer calls
`process()`. A process call promotes work due at entry and drains a snapshot, so
tasks submitted by callbacks wait for a later call. A configured exception
handler lets processing continue; without one, unstarted work from the batch is
restored and the exception is rethrown. Its notifier is non-owning and requires
producer quiescence before reset or destruction.

This is a strong match for generic task scheduling, but two upstream gaps block
direct use in bounded deterministic imtg paths:

1. submission cannot reject because of a configured queue capacity; and
2. delayed and periodic paths call the concrete monotonic clock directly.

Those are generic library concerns. They should be separate upstream changes so
existing unbounded callers remain source-compatible and deterministic-clock
tests do not become coupled to capacity work.

`EventBus`, `EventEndpoint` and `EventAwaiter` cover much of the optional event
surface. The important limit is that an awaiter's deadline is passive. No
background timer completes it; a bus poll point must occur. Also, removing a
subscription or closing an endpoint is not, by itself, proof that a callback
already selected by another thread is finished. The imtg adapter therefore
keeps the lifetime guard and drained-state rules from
`EVENTS_AND_LIFETIME.md`.

`ModuleHub` and `RunLoop` already cover passive and active driving. A future
integration should choose them instead of constructing another generic loop.
They do not, however, implement the keyed mailbox contract required by imtg:
parallel different keys, FIFO per key, no same-key overlap, bounded global and
per-key admission, observable worker failure, and controlled drain. That gap is
broadly reusable and is a candidate upstream `KeyedSerialExecutor`.

### kurlyk

The audited HTTP stack already has both global pending admission and group-level
in-flight admission. `HttpRequestManager::submit_request()` returns a typed
result with `QueueLimitExceeded`; `HttpClient` serializes its group admission
check with submission. Cancellation exists by request and group, and group
completion is observable through blocking or timed waits. The network worker
owns processing and shutdown; its managers must remain alive while registered.

Retry attempts, retryable status handling and rate limiting are also present.
General and specific rate-limit handles can be shared, optionally keyed, and a
sequential limit holds an in-flight token until completion. In
`RateLimitDelay`, `Duration::max()` together with `sequential_blocked=true` is a
sentinel meaning “wait for the current sequential request”; it is not a real
infinite timer.

The audited `HttpRequestHandler` retry predicate is precise and narrower than
an HTTP retry allowlist:

```text
should_retry =
    retry_attempts != 0 &&
    (has_curl_error || !has_valid_status) &&
    retry_attempt < retry_attempts &&
    !has_stream_chunk
```

Here `valid_statuses` is the set of statuses accepted as successful by the
request; it is not a list of statuses permitted for retry. A CURL failure is
independently retry-eligible when automatic retries are enabled. Setting
`retry_attempts = 0` disables automatic retry. Configuring `retry_attempts = 3`
allows at most three total attempts in this implementation (the initial
attempt plus two retries, because the counter is incremented before the
comparison). Once a streaming body chunk has been emitted, the request is not
automatically retried, avoiding a second partial stream. Intermediate stream
callbacks carry `response->ready == false`; only the final callback sets
`ready == true`.

Status `499` is used by the library for client cancellation and `451` may be a
synthetic or transport-level status in a test/adapter path. Neither number by
itself proves that a remote server rejected the operation. A Telegram adapter
must inspect the CURL error, `ready`, response body and method contract before
classifying an attempt as definitely rejected, retryable, or
`UnknownOutcome`.

Generic per-request HTTP/CURL retry allowlists are not an existing public
kurlyk capability. If needed, that policy is a separate upstream extension;
imtg must not imply that `valid_statuses` already provides it.

imtg must not build a second generic network manager, HTTP retry loop or rate
limiter. The M3 adapter instead supplies:

- Bot API request/response encoding and method classification;
- chat/method-specific rate-limit keys and Telegram `Retry-After` mapping;
- durable effect/attempt identity and retry eligibility;
- the transition to `UnknownOutcome` when acceptance may have happened; and
- staged shutdown coordinated with the persistent outbox.

Generic retries are safe only for outcomes the adapter classifies as definitely
not accepted or for operations whose verified semantics permit repetition.

### time-shield-cpp

`DeadlineTimer` and `ElapsedTimer` are compact single-thread monotonic helpers.
They expose overloads that evaluate against a supplied time point, which is
useful for deterministic policy code. `TimerScheduler` is a different service:
it may be driven manually or own a dedicated worker, schedules fixed-rate and
single-shot callbacks, and offers synchronous `stop_and_wait()` for an active
timer callback.

The scheduler is not bounded persistent work. Its priority queue may retain
cancelled generations until they reach the front, and its own documentation
notes temporary growth under frequent start/stop cycles. It calls
`steady_clock::now()` directly and catches callback exceptions without an
observable handler. Consequently, it is not the default imtg job/executor
choice. `event-hub-cpp::CalendarScheduler` already integrates TimeShield for
calendar semantics and already supplies a wall-clock provider plus missed-run
and overlap policies; those features must not be reimplemented in imtg.

### mdbx-containers

`Transaction` owns a native MDBX transaction, aborts an outstanding handle on
destruction, and exposes `commit()`/`rollback()`. Transactions and cursors are
thread-bound; moving a live guard does not authorize use on another thread.
The wrapper reports native errors through exceptions and does not expose the
core `CommitResult`/`CommitOutcome` protocol. An imtg backend must therefore map
pre-commit failures, native commit results and process interruption
conservatively, then use persisted identities/revisions for recovery instead of
assuming that an exception proves absence.

The table layer supplies the physical pieces needed by M2d:

- canonical order-preserving `CompositeKey` encoding for two to five typed
  components;
- ordered lower/upper-bound and range/callback APIs backed by MDBX cursors;
- early callback termination for bounded scans; and
- `TableSequence`, whose local per-DBI allocation participates in the caller's
  transaction.

The adapter must still define the imtg schema, record codecs, migration gates,
DBI budget, continuation-token encoding, revision comparisons, claim leases and
fencing. It must open with a durability configuration appropriate to the
runtime; the audited `Config` defaults `sync_durable` to true, but the adapter
must validate rather than silently inherit deployment settings.

The experimental sync subsystem contains a durable ordered
`LogicalOutboxStore`. Its sequence, route and cumulative acknowledgement model
is explicitly for logical replication. It is valuable evidence that atomic
enqueue, ordered prefix scans and durable acknowledgement frontiers can be
built with the underlying primitives. It is not interchangeable with an imtg
outbox row, whose identity, claim lease, Telegram attempt state, fencing and
unknown outcome are different. A replication outbox must never be relabelled as
a business-effect outbox.

### optionx_cpp

The Telegram bridge is a reference, not a dependency candidate. Its source is a
`TelegramMessageSource` that promises no callbacks after `stop()` returns; the
concrete template adapter consumes a `tg-client-stdio`-compatible worker.
The accompanying design explicitly uses a Telethon user-client authorization
flow and keeps MTProto/session handling outside C++. This is materially
different from imtg's planned Bot API HTTP delivery through kurlyk.

Several lifecycle choices are reusable as engineering evidence:

- callbacks capture shared runtime state instead of a destructible bridge
  object;
- concurrent source intake is serialized across allocation and dispatch;
- dedupe and strategy state are reserved before invoking user code;
- allocator failure can roll back a reservation, while a throwing dispatch
  callback is classified as ambiguous and retained fail-closed; and
- watchdog shutdown avoids joining its own thread and defers final join to an
  external path when necessary.

These are patterns to test in imtg; they do not justify importing the trading
models, parser, worker protocol or user-client transport.

## Integration constraints

1. `imtg::core` remains independent of all five audited libraries. Optional
   integrations live behind imtg-owned interfaces and CMake targets.
2. A dependency is added only by the implementation PR that consumes it, as a
   pinned direct submodule under `external/<name>`. The audit clones are never
   committed.
3. Adapters translate dependency exceptions and callbacks into the existing
   imtg failure model. No dependency exception crosses a `noexcept` worker
   boundary.
4. Runtime queues receive explicit non-zero limits. A dependency's “zero means
   unbounded” default is not an acceptable production configuration by
   accident.
5. Transactions, cursors, callbacks and non-owning notifiers keep their audited
   ownership/thread affinity. Shutdown ordering is tested, including calls made
   from callbacks.
6. Persistent work remains represented by storage records. In-process task,
   timer and event queues are wake-up and execution mechanisms, never the sole
   record of a required effect.
7. The MDBX adapter must pass the same backend conformance suite as the
   in-memory backend plus process-restart and durability tests. Logical protocol
   success on the in-memory backend is not physical crash-durability evidence.
8. Telegram delivery tests use a fake kurlyk-facing transport and cover queue
   rejection, cancellation, 429 delay, definitely-failed attempts, ambiguous
   completion, stale claims and shutdown with durable pending work.

## Resulting work order

The audit authorizes design work; it does not mark any integration implemented.

1. Keep the current imtg `ReferenceRuntime` unchanged while preparing separate
   upstream `event-hub-cpp` work for bounded `TaskManager` admission, an
   injectable monotonic clock, and a generic keyed serial executor.
2. Define M2c render-intent claim/lease/fencing, bounded continuation scans,
   reconciliation, atomic TX2 and recovery/unknown-outcome boundaries in a
   separate imtg design PR. This does not need to wait for executor work.
3. Implement and test the in-memory logical M2c protocol before claiming
   process-crash durability.
4. Add the optional `mdbx-containers` M2d adapter only when its schema and
   transaction-result mapping are reviewed. Reuse table/transaction primitives;
   implement imtg repositories and effect outbox records in the adapter.
5. Start M3 with a kurlyk adapter; do not add another HTTP worker, retry engine
   or rate limiter. Keep Telegram-specific persistence and uncertainty in imtg.
6. Defer a direct TimeShield dependency and any OptionX integration until a
   concrete requirement survives the existing event-hub/kurlyk boundaries.
