# ADR 0012: optional validated scenario packages

**Status:** Proposed; implementation is deferred until the M4 composition
track after the first alpha and the M3 gateway boundary.

## Context

The reference application plan needs a reusable way to describe multi-step
intake, support and commerce workflows without putting every application's
states and prompts into the core runtime. An external Telegram project shows a
useful package shape: a graph, external stage instructions and schemas, plus an
offline graph exporter and QA tools.

That project also demonstrates why a declarative file cannot be the authority
for execution. A transition condition may be descriptive rather than enforced,
state and stage variables may be persisted in separate writes, and automatic
progression can loop without one shared budget. imtg already has stronger
typed FSM, transaction, revision and effect-recovery boundaries that must not
be weakened for authoring convenience.

## Decision

Add a future optional `imtg_scenarios` module with these properties:

- a scenario package is versioned graph metadata, bounded resource references,
  typed stage input/result schemas and an explicit execution policy;
- the package loader validates identities, references, reachability, terminal
  states, cycles, schemas and version compatibility before admission;
- the loader produces the same validated graph representation used by the
  existing `imtg::Fsm`; programmatic C++ registration remains supported;
- events and LLM results are proposals only. Typed guards, authorization,
  current revision and application context are enforced by the runtime inside
  its serialized transaction boundary;
- required state, stage data, processed-update identity and render/effect intent
  use the existing atomic storage contracts;
- automatic progression consumes one finite budget for steps, model calls,
  bytes/cost and local deadline; exhaustion is explicit and fail-closed;
- an offline simulator and DOT/Graphviz exporter are part of the development
  boundary, not a new GUI or Telegram dependency; and
- prompts, model clients, provider credentials and business records remain
  application-owned and are not introduced into `imtg_core`.

## Consequences

Positive:

- one validated scenario can be presented by a bot, a future Mini App or an
  operator tool without duplicating its business transitions;
- graph validation and offline traces make unreachable states, accidental
  cycles and schema drift visible before transport integration; and
- optional packages preserve the small core and allow applications to keep the
  ordinary C++ registration path.

Costs and limits:

- a loader must define a stable package/schema version and migration or
  rejection policy;
- resource and execution budgets become part of the public module contract;
- declarative metadata cannot express arbitrary application policy without an
  explicit typed extension; and
- scenario support does not provide LLM correctness, provider deadlines or
  payment durability by itself.

## Rejected alternatives

1. **Make YAML/JSON the only FSM API:** rejected; small applications should not
   pay for parsing or lose compile-time registration.
2. **Let scenario metadata or LLM output execute guards/effects directly:**
   rejected; this bypasses authorization, revisions and transaction fencing.
3. **Put scenario loading in `imtg_core`:** rejected; it adds a format,
   resource and optional model/tooling surface to the core library.
4. **Copy the external project's implementation or prompts:** rejected; the
   reviewed revision has no declared license and its code is not a dependency.

## Gates

Implementation requires deterministic tests for package validation, resource
containment, guard enforcement, atomic state/result/effect planning, cycle and
budget termination, offline replay, graph export and redacted QA output. The
module remains optional and independently buildable.
