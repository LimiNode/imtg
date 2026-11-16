# `tg-statemachine-bot` capability audit

**Status:** research input for optional M4 application composition. **Reviewed
revision:** `64f46d14215d328ec27c288ad2ace697d1665565`. This document records
source observations and the boundaries that are useful for imtg. It does not
add a dependency, copy source code, or claim that scenario support is
implemented.

## Source and licensing boundary

The reviewed project is [`chechestor/tg-statemachine-bot`](https://github.com/chechestor/tg-statemachine-bot) at the exact revision above. The inspected locations are:

- [`dialog_fsm.yaml`](https://github.com/chechestor/tg-statemachine-bot/blob/64f46d14215d328ec27c288ad2ace697d1665565/dialog_fsm.yaml);
- [`fsm/fsm.py`](https://github.com/chechestor/tg-statemachine-bot/blob/64f46d14215d328ec27c288ad2ace697d1665565/fsm/fsm.py);
- [`conversation_core/conversation_core.py`](https://github.com/chechestor/tg-statemachine-bot/blob/64f46d14215d328ec27c288ad2ace697d1665565/conversation_core/conversation_core.py);
- [`tools/generate_fsm_drawio.py`](https://github.com/chechestor/tg-statemachine-bot/blob/64f46d14215d328ec27c288ad2ace697d1665565/tools/generate_fsm_drawio.py);
- [`bot/qa/`](https://github.com/chechestor/tg-statemachine-bot/tree/64f46d14215d328ec27c288ad2ace697d1665565/bot/qa).

GitHub reports no declared license for this revision (`license: null`). The
repository is therefore an architectural and behavioural reference only. imtg
must not copy its implementation, prompts, schemas, or assets without a
separate rights review.

## Observed ideas worth carrying forward

The project separates a scenario description from the engine that executes a
conversation. A scenario combines:

1. a state/transition graph;
2. stage instructions kept as external Markdown resources; and
3. typed stage inputs and results represented by schemas.

The same shape can support an intake bot, support workflow, shop flow, or an AI
assistant without putting every dialog into the runtime binary. The useful
boundary is the scenario package, not YAML itself:

```text
scenario package = graph + resource references + data schemas + version
```

The project also provides two practical development aids: a graph exporter and
a QA/debug contour that can be enabled for a test profile. Both are valuable
because a scenario graph is easier to review and test outside Telegram, an LLM,
or a persistent database.

## imtg decision

M4 may add an optional `imtg_scenarios` module. It is a loader, validator and
simulator around the existing typed FSM/session and application-composition
contracts; it is not a replacement for `imtg::Fsm` and does not become a core
dependency.

The normative C++ registration path remains available for small applications.
YAML/JSON is an optional interchange and authoring format whose loader must
produce the same validated graph representation. A scenario file cannot
register arbitrary C++, bypass authorization, or publish an effect directly.

### Required loader validation

Before a package is admitted, the optional module must check:

- package and schema version compatibility;
- unique state and transition identities;
- valid initial, source and destination states;
- resource references within the package root and within explicit size limits;
- terminal-state declarations and reachability from the initial state;
- whether cycles are intentional and bounded by the execution policy;
- input/result schema references and required fields; and
- deterministic diagnostics for every rejected package.

`condition` or `guard` metadata is descriptive until it is bound to an
application-owned, typed predicate. A loader must never treat a string from a
scenario file or an LLM response as an authorization decision. The runtime
evaluates guards against the current state, revision, identity and application
context inside the existing serialized transaction boundary.

### Execution and trust boundary

External events and LLM responses may propose a typed event or stage result;
they do not become trusted state automatically. The application validates the
proposal, applies the FSM guard, and commits state, processed-update identity,
and any required render intent through the existing transaction contract.

Stage data should carry an explicit type/schema identity, owner, version and
provenance. Business records remain domain-owned; they must not be hidden in a
conversation session merely because a scenario refers to them.

Automatic transitions require one finite budget covering at least transition
steps, LLM calls, input/output bytes, configured cost and local deadline. A
cycle, retry storm or exhausted budget returns an explicit failure and leaves
the last committed state authoritative. The budget is a safety boundary, not a
claim about a provider's remote deadline.

### Offline tooling

The first useful implementation slice is a small validator/simulator CLI and a
DOT/Graphviz exporter. It should run without Telegram, an LLM or a database and
support deterministic event traces, unreachable/dead-end/cycle diagnostics,
schema failures and budget exhaustion. QA snapshots and raw model responses are
test-profile artifacts only; they must be redacted and must not expose tokens,
personal data or private prompts in ordinary operator output.

## Lessons not to copy

The reviewed implementation contains several warnings that matter to imtg:

- transition `condition` metadata is not itself a programmatic guard; the
  runtime must enforce guards independently;
- saving state and stage variables in separate writes can expose a partial
  transition; imtg must keep required state, result and effect planning inside
  one transaction boundary;
- an unbounded `while True` style of automatic progression needs a shared
  transition/LLM/cost/deadline budget;
- a hard-coded initial state or a version field without migration policy does
  not make scenario replacement safe; and
- creating an external payment before recording a local attempt leaves an
  uncertain boundary. imtg's `PaymentAttemptId`, idempotency and
  `UnknownOutcome` contracts remain the required pattern.

These are design constraints, not claims that the external project is an imtg
implementation target.

## Roadmap and acceptance gate

The scenario-package work belongs after the first alpha and after the M3
gateway contract is stable enough to exercise one real application. A future
implementation PR must prove, with fake storage and gateway adapters:

1. invalid packages fail before execution with deterministic diagnostics;
2. external/LLM proposals cannot bypass typed guards or authorization;
3. state, stage result and required effect intent commit atomically;
4. cycles and automatic steps stop at the finite budget;
5. an offline trace reproduces the same state/result sequence; and
6. graph export and QA output obey the package-root and redaction boundaries.

Until those checks exist, `imtg_scenarios` remains planned documentation and
the existing C++ FSM remains the only implemented state-machine boundary.
