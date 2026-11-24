# Architecture remediation plan — 2026-11-21

**Status:** staged work, not merged or operationally accepted. This plan orders
the accepted review work; the owning contracts remain normative. Behavioral
corrections precede structural moves. Every independently reviewable slice is
a Draft PR with its own tests, contract/status updates and exact HEAD evidence.
No merge, tag, Release, live credentials or production migration is authorized.

## Baseline and ordering

Preserve composition `467f6f467dcc0b36b5006e6e390b749b9aaeaab9` (tree
`3c184c82bca9526754c54e2425df092efa22c52e`) as the common source baseline.
It combines the accepted logging/outgoing correctives and documentation; it
does not integrate the incoming kurlyk server. See the
[composition evidence](research/M3_COMPOSITION_EVIDENCE_2026_11_21.md).

| Slice / intended PR boundary | Dependency | Observable acceptance |
| --- | --- | --- |
| A. Publish original composition and combined CI | Existing composition | Original SHA remains reachable; 11-pin flat audit before configure; Linux Debug/Release enables commerce, MDBX, Telegram, generic transport, kurlyk and mandatory logging together; installed consumers/headers run; existing normal/adapter workflows remain |
| B1. Callback capture retirement (RT-01, I2) | A source baseline | Reset, close and registry removal permit reentrant capture destruction; no user destruction under mutex; external close waits for callback/capture quiescence; subprocess timeout regressions red before fix, green after |
| B2. MDBX exception/revision correction (ST-1, ST-2) | A source baseline | Allocation failures cannot leave a publishable divergent transaction; prior staged writes cannot leak; common virgin/tombstone create/erase/recreate revisions and watches agree with InMemory |
| B3. Persisted/session invariants (S-01, S-02) | A source baseline | Shared render-intent write/decode validation rejects zero session revision; invalid metadata mode/value combinations cannot commit a processed marker |
| B4. UI builder exception safety (I1) | A source baseline | Allocation failure during node, parent-link or scope-stack mutation leaves either unchanged state or a failed render, never a successful orphan tree |
| B5. Checked time and typed ordering (TC-01, TC-03, TC-04) | A source baseline | Extreme token expiry and ManualClock conversion/addition do not overflow; typed ConversationKey ordering is allocation-free, numeric and consistent; wire encoding unchanged |
| B6. Stable payment provider identity (I3) | A source baseline | Provider lookup has no owning-string allocation inside a noexcept identity operation; implementations, callers, contract and tests change together |
| C. Neutral kurlyk server boundaries | Accepted kurlyk #52 | DTO-only include needs no vendors; compile-time fake backend and existing network tests pass; C++11/17, install bridge, combined client/server consumer and lifecycle preserved |
| D1. MDBX provider resolution | B2 | Existing compatible target precedes explicit provider and own pinned source; absent unused paths are ignored; parent normal/cache options preserved; offline failure and real consumer fixture |
| D2. Storage decomposition and existing table API reuse | B2, D1 | Preopened long-lived wrappers, one explicit transaction, native read-your-writes, metadata-only pending; previous bytes reopen; multi-table commit/rollback, binary data, ranges and ownership tests |
| E1. Header audit enforcement | A source baseline | Resolve real public/private/source-only edges; detect quoted and spaced includes, upward edges and cycles with negative fixtures; include MDBX sources |
| E2. Core/runtime/UI domain decomposition | B1, B4, B5, E1 | Narrow error/result and data entries; private notification state hidden; runtime's reference reconciler retained; deterministic umbrellas and standalone header tests |
| E3. Sessions/reconciliation decomposition | B3, D2, E1 | Data, codecs, repositories, claims, coordinators, FSM, planner and atomic publication separated without weakening fencing or changing wire semantics |
| E4. Telegram data boundary | E1 | Real telegram/data/ files and data.hpp umbrella for implemented groups; vendor/router/runtime/JSON-free DTO; normalized events explicitly distinguished from protocol objects |
| E5. Remove compatibility shims | E2–E4 | Remove only core/core.hpp, runtime/runtime.hpp, ui/ui.hpp, actions/actions.hpp and actions/registry.hpp; update manifest, whitelist, tests and migration guide; storage/storage.hpp is not a shim |
| F. TimeShield reuse | B5 | Retain ClockTime/IClock/ManualClock; private SystemClock source and conversions reuse suitable existing API; direct dependency independent of parent logger; rounding/overflow tests and documented wall-clock semantics |
| G. First complete Telegram protocol slice | C, E4, architectural acceptance | User/chat/message/entities, callback variants, keyboards, request DTO, typed update decode and webhook management delivered as complete groups with explicit coverage table |

B1–B6 and C can proceed independently after the baseline is published. Do not
combine behavior changes with large file moves. The tables describe work to
perform, not completed acceptance. New evidence is recorded per PR; a passing
individual PR is not combined-tree evidence.

## Current corrective batch — 2026-11-24

| Slice | Published Draft | Scope and remaining gate |
| --- | --- | --- |
| A | [imtg #42](https://github.com/LimiNode/imtg/pull/42) | Original composition is GitHub-accessible; combined CI added; exact source/run evidence is linked from the PR |
| B2 | [imtg #43](https://github.com/LimiNode/imtg/pull/43), `458e39c8dcc2ba7a314132489125ab0cf82d6d5d` | ST-1/ST-2 and test injection corrected; exact-head normal 17/17 and adapter 7/7 succeeded, including ordinary MSVC Debug. Independent review remains; see the [diagnosis](research/MSVC_ALLOCATION_HARNESS_2026_11_24.md) |
| B1 | [imtg #44](https://github.com/LimiNode/imtg/pull/44), `038bdd73c613f9870ac0b754bebb19615dd4b076` | Retirement/quiescence and registration ownership fixed; libc++ fixture corrected; both exact-head workflows succeeded; independent review remains |
| D1 | [imtg #45](https://github.com/LimiNode/imtg/pull/45), `bb8e2c4437c8ba38d96e3dd01d73cdc953414e4a` | Updated onto verified B2 without rewriting history; provider diff unchanged. Seven consumers/two expected configure failures retained; current local/hosted results are recorded in the PR |
| B3 | [imtg #46](https://github.com/LimiNode/imtg/pull/46), `4fb03f322859967337c8f96eb188a0d2cbeb5641` | Shared intent validation and pre-transaction metadata admission; common InMemory/MDBX regressions; both workflows succeeded; independent review remains |
| B4 | [imtg #47](https://github.com/LimiNode/imtg/pull/47), `12817552c0ff0130aa97c784fce5995498e5f8db` | Mutation exceptions make the render terminal; allocation regressions and both workflows succeeded; independent review remains |
| B5 | [imtg #49](https://github.com/LimiNode/imtg/pull/49), `652c1019acedf4dd6ebc522d8d012ec1a326bcf7` | Checked duration conversion/addition and numeric ordering; both workflows succeeded; independent review remains. Full TimeShield integration remains F |
| B6 | [imtg #48](https://github.com/LimiNode/imtg/pull/48), `a4d1d311f9efd8aa759277bb2bd9138f1e56fbb7` | Lifetime-bound provider identity reference and long-ID allocation prohibition; both workflows succeeded; independent review remains |
| E1 | [imtg #50](https://github.com/LimiNode/imtg/pull/50), `f890961f30d5da4e39a95986d197dac0f2c39c6d` | Real include graph, source-only classification and 15 fixtures; both workflows succeeded; independent review remains |
| C | [kurlyk #53](https://github.com/LimiNode/kurlyk/pull/53), `c59795c8a05fa4ed2a167273b6dcad94d386cf3e` | Separate server slice, current 10/10 succeeded; review/incorporate before merging #52. Not exercised by imtg's outgoing composition |

These source HEADs are composed separately from #42 in
Draft [#51](https://github.com/LimiNode/imtg/pull/51),
`integration/architecture-corrective-batch`; its own commit/tree and test evidence
are recorded in the [corrective composition note](research/CORRECTIVE_COMPOSITION_2026_11_24.md).
Sibling CI is not combined-tree evidence. The registration finding and MSVC
timeout are not closed by historical pre-corrective runs. All PRs remain Draft.

D2 is the next storage slice only after acceptance of this batch. E2–E5, F and
G are not implemented here. No merge, main mutation, tag, Release, incoming
server, webhook or new upstream transaction API belongs to this corrective.

## MDBX decision (supersedes the earlier upstream proposal)

Pinned `mdbx-containers@4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` already
supports explicit/shared `Transaction`, commit/rollback, `KeyValueTable` CRUD
and stoppable ranges, and `MetadataTable`. No new transaction-binding, generic
CAS or versioned-record upstream API is a prerequisite.

Open table wrappers once before intake, not inside each active transaction.
Use an explicit existing transaction for every related table operation and
one owner commit. Respect the wrapper's no-concurrent-access contract through
per-worker ownership or short external synchronization, including reads and
ranges; do not invoke application callbacks under that lock. Retain transaction
thread ownership and destroy wrappers before their connection closes.

imtg still owns expected revisions, tombstones/ABA, candidate revisions,
namespace/DBI mapping, key bounds, IMR1/IMS1 codecs and error/Unknown mapping.
Keep existing IMS1 bytes through ordinary KeyValueTable unless a separately
reviewed migration chooses MetadataTable's tagged format. Logical-sync
VersionedKeyValueTable has different semantics and is not a replacement for
local expected-revision checks. A narrow bootstrap helper for atomic schema/DBI
initialization may remain; duplicate native CRUD/cursor implementations should
not. Optimize SessionRepository's repeated serialization only after choosing a
compatible policy for the diagnostic payload revision.

New upstream work requires a concrete operation the pinned API cannot express.
The same rule applies to TimeShield: inspect existing checked conversions and
rounding first; any missing generic primitive gets an independent contract,
tests and review before an imtg pin update. Storage's round-to-future policy is
not interchangeable with truncation, especially for negative timestamps.

## Server and protocol boundaries

Preserve <kurlyk/http_server.hpp>, kurlyk::http_server and HttpServer::Handler.
Separate neutral config/request/response/handler data, shared route/response
policy, public facade and concrete SimpleWeb adapter. Use an internal
compile-time backend contract and a tiny fake, not a public virtual hierarchy.
Keep one lifecycle authority, caller-owned run, one-shot start, abortive
stop/destruction from a handler and quiescence. Preserve receive bounds,
duplicate headers and HEAD/204/205/304 behavior. Move the installed-layout
header bridge together with its template, guard, destination and exclusion.

Current Telegram types are normalized router events, not a complete official
Bot API model. Create real data groups now, without empty placeholder files or
parallel near-duplicate DTOs. Subsequent protocol groups require DTO + codec +
methods/handling + tests, explicit optional/variant semantics and a coverage
table. Unsupported update kinds get an explicit result; unknown fields need
not retain a JSON DOM. JSON remains private.

The future imtg webhook handler owns authentication, trusted endpoint bot ID,
bounded decode, routing admission and ACK policy, not the listener or threads.
RAM admission never establishes durable acknowledgement; durable inbox and
recovery precede that claim. An outgoing live HTTPS smoke requires a separate
explicitly selected test chat and authorization; offline tests do not imply it.

## Invariants, cleanup and evidence limits

Preserve strong identifiers, conversation scope semantics, clock injection,
unit-of-work and fencing checks. A possible ChatScope/UserInChatScope rename
is a separate API decision; no wire change is implied. Do not invent a blanket
ban on zero IDs. Remove meaningless encode().empty() checks only after checking
the real invariant. Keep MoneyScalePolicy validation without a justified
replacement. Token-parser dead fields, unused retained encoders and inaccurate
ActionContext lifetime wording are small follow-ups, not reasons for a rewrite.

Header architecture remains the accepted hybrid DAG: domain umbrellas for the
main library and self-contained narrow components/optional modules. Security
context duplication must not be removed merely to reduce field count; choose
one canonical fencing context only after mechanical separation and tests.

Each implementation slice requires focused active CHECK regressions, relevant
Debug/Release suites, first-include/installed consumers, Doxygen where public
API changes, whitespace/link checks and independent review. Record sanitizer
or platform coverage only when actually run. InMemory tests do not establish
crash durability; source-only MDBX is not an installed MDBX package. Existing
root rebase/user changes remain untouched in isolated worktrees.
