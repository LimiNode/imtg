# Telegram application capability audit

**Status:** research baseline for M3–M6 planning. **Audit date:** 2026-11-06.
This document records source observations and architectural decisions. It does
not add a dependency, copy application code, or claim that an imtg capability
is implemented.

## Method and licensing boundary

The reviewed revisions below are pinned by commit SHA so that a later
implementation review can reproduce the observation. A source location means
the path inspected at that revision; a README claim was not treated as proof
unless the corresponding implementation or test was also inspected.

GPL and unlicensed/`NOASSERTION` projects are reference material only. imtg
will not copy their source, generated assets, schemas, or domain policies. A
future adapter must have its own implementation and an explicit license review.

## Reviewed sources

| Project | Exact reviewed SHA | License / maintenance | Source locations inspected | Decision |
|---|---|---|---|---|
| `aiogram/aiogram` | `e164475d00dcbc607da4bfca3f8154cad800028f` | MIT; active | `aiogram/dispatcher/`, `aiogram/filters/`, `aiogram/fsm/`, `aiogram/client/session/middlewares/`, payment types | Architecture reference for typed routing and middleware; implement an imtg-owned boundary |
| `Tishka17/aiogram_dialog` | `b6dfeff45da33ad5b20baa1ff85e3fb12d20daf4` | Apache-2.0; active | `src/aiogram_dialog/`, `example/mega/bot_dialogs/`, `docs/quickstart/` | UI/forms reference; optional module, not imtg core |
| `python-telegram-bot/python-telegram-bot` | `8dec234225145f8ec0cc8fd97d42208a32d8f0e6` | GPL-3.0; active | `src/telegram/ext/filters/`, `src/telegram/ext/handlers/`, `examples/paymentbot.py`, `src/telegram/_payment/` | Behavioral reference only; no source reuse |
| `PaulSonOfLars/tgbot` | `19f837db527ed5acd061213b7858c4fadacd7e14` | GPL-3.0; archived | `tg_bot/modules/antiflood.py`, `tg_bot/modules/cust_filters.py`, `tg_bot/modules/sql/antiflood_sql.py` | Historical moderation reference; do not copy |
| `group-butler/GroupButler` | `63605252e0c677ff8dbbbe44f8369276c0b29023` | GPL-2.0; low activity | `lua/groupbutler/plugins/antispam.lua`, `lua/groupbutler/plugins/service.lua` | Historical rules/anti-flood reference; do not copy |
| `redstone-md/shield` | `6591591e6ab9140e9b75362057827bde92fa2a4f` | MIT; active | `app/bot/spam.go`, `app/controlplane/`, `app/audit/`, related tests | Strong fast-path, review and audit reference; possible independent optional module |
| `NikitaUshakov/telegram-antispam-bot` | `0e2afa16d2e99731020282c4e506bdbf11c5e067` | No license detected; active | `app/antispam/captcha.py`, `flood.py`, `profile_score.py` | Behavior reference only; no reuse until a license is supplied |
| `ilyarolf/AiogramShopBot` | `f886de2519c71bd6eae008b61c3c43d9822591aa` | MIT; active | `services/cart.py`, `services/buy.py`, `repositories/payment.py`, `middleware/throttling_middleware.py` | Scenario reference; payment/fulfillment must be independently designed and verified |
| `JumpCodeFrog/telegram-shop-bot` | `b08608706f330d8f732d92959eb014f58009e9ce` | MIT; active | `internal/bot/handlers_payment.go`, `internal/bot/payment_ack_test.go`, `internal/bot/out_of_stock_quarantine_test.go`, `docs/adr/0001-commerce-state-and-payment-ledger.md` | Useful state/ledger and failure tests; optional application reference |
| `prettyleaf/telegram-support-bot` | `68ffe135bc80058b8ea0f569d761e192ce6b251d` | No license detected; active | `internal/services/ticket_service.go`, `internal/database/migrations/`, media metadata migrations | Ticket scenario reference only; no source reuse |
| `mikhail494/telegram-ticket-system` | `a545aec0039666dbe2508b0b11b8ec3958e92904` | `NOASSERTION`; active | `src/ticketRouting.ts`, `src/persistence/ticketsRepository.ts`, `src/supportIngressLimiter.ts`, `test/bot/ticketStatus.test.ts` | Recovery/limits reference only; licensing must be resolved before reuse |
| `Talonmortem/service-request-bot` | `c0320219fc98c8644de2e4e14770d479b03f4fb4` | No license detected; active | `app/service.py`, `tests/test_service.py` | Small intake-flow reference only; no source reuse |
| Miss Rose documentation | N/A (closed-source product reference) | Product documentation, not source | official AntiRaid/Captcha/locks/antiflood documentation | Feature vocabulary and operator controls only |
| `LimiNode/event-hub-cpp` | `2d4b09763603db2e5f9c5ca5bce7de3ffc9332b0` | Existing LimiNode audit | `TaskManager`, `EventBus`, `ModuleHub`, `RunLoop` | Reuse/adapt only behind existing imtg lifetime and backpressure contracts |
| `LimiNode/kurlyk` | `b62cd53f672c47b74bb14d762ea9dc59a9df19ce` | Existing LimiNode audit | request admission, cancellation, retry and rate-limit paths | Future Telegram transport adapter; not a durable outbox |
| `LimiNode/mdbx-containers` | `4ab32b5eab3cdfce95d1a6c0f05f566019793ec4` | Existing LimiNode audit | transactions, ordered tables, sequences, logical outbox | Optional M2d backend primitives; not an application effect model |
| `LimiNode/optionx_cpp` | `982413438755038982d990f251c77ecb09f2e294` | Existing LimiNode audit | `TelegramSignalBridge` lifecycle and ambiguous callback handling | Reasoning reference only; not a Bot API dependency |

The LimiNode rows reuse the exact revisions already recorded in
`docs/DEPENDENCY_CAPABILITY_AUDIT.md`; they are not a second dependency audit.

## Capability matrix

| Capability | Verified source behavior | imtg classification | Milestone and acceptance proof |
|---|---|---|---|
| Typed update routing, filters and middleware | aiogram separates routers, filters, middleware and FSM context; PTB provides typed handler/filter composition. The reviewed aiogram behavior is routing/filter/middleware evidence, not a generic deadline-aware priority scheduler. | Shared framework capability at the Telegram boundary; one imtg router, not a second dispatcher. Deadline/priority scheduling is an imtg architectural requirement. | M3: routing and scheduling tests prove deterministic filters, middleware order, deadline admission, fairness, filter failure and explicit overload outcomes |
| Headless handlers | Moderation and payment examples handle message/member/payment updates without rendering a dialog | Shared runtime boundary; a handler may commit domain state or effects without a `RenderIntent` | M3: a message moderation and `pre_checkout_query` path runs with no UI session/render intent |
| Per-key serialization and bounded admission | imtg M1 runtime already provides keyed mailboxes and bounded queues; kurlyk provides bounded HTTP admission | Already implemented foundation plus transport adapter; do not duplicate event-hub or HTTP queues | M3: flood tests show bounded memory, per-chat ordering and `QueueFull`/`Closed` outcomes |
| Urgent payment updates | Telegram payment examples expose pre-checkout and successful-payment events; the Bot API imposes a short pre-checkout response window | imtg-owned typed update class with bounded priority/deadline policy; domain verification remains commerce module | M3: expired/deadline and duplicate payment tests; gateway never waits behind ordinary rendering work |
| Dialogs, forms and pagination | aiogram-dialog provides dialog stacks, widgets, navigation and examples of multi-step input | Optional `imtg_forms`/`imtg_dialogs` modules using FSM/UI contracts | M4: draft/resume, field validation and locale snapshot tests |
| Text/moderation fast path | Shield separates message detection, control-plane dictionaries and audit; tgbot/GroupButler show rules and flood controls | Optional `imtg_moderation`; policy and evidence are application-owned | M5: bounded fast path, dry-run, reversible sanction and audit tests |
| Join CAPTCHA and anti-raid | `telegram-antispam-bot` implements flood/profile scoring and CAPTCHA paths; Miss Rose documents operator controls | Optional guardian module; not core and not a universal ban policy | M5: configurable Normal→Elevated→Raid→Lockdown→Recovery simulation |
| Catalog, orders, payment ledger and fulfillment | Shop examples separate cart/order/payment handlers and include stock/payment tests; one example documents quarantine for out-of-stock | Optional `imtg_commerce` domain module; payment identity independent of callbacks | M5: duplicate payment, unique-item allocation, refund and delivery-Unknown recovery tests |
| Ticket/support workflows | Support/ticket examples use forum topics, persistence, status transitions, ingress limits and media metadata | Optional `imtg_forms` + `imtg_tickets`; business fields stay outside core | M5: resume, duplicate update, concurrent admin transition, notification Unknown, restart persistence |
| HTTP, retry and rate limits | kurlyk provides queue/group admission, cancellation and rate-limit mechanics | Adapter; imtg owns Telegram method policy and durable uncertainty | M3: fake transport tests for 429, cancellation, retry budget and UnknownOutcome |
| Durable timers/jobs/TTL | event-hub/time-shield provide passive scheduling primitives but not durable recovery or bounded admission in every path | Shared M5 contract; adapter may reuse mechanics, storage/recovery remains imtg-owned | M5: restart, lease, bounded queue and missed-deadline tests |
| Audit and history | Shield and ticket projects record decisions/statuses; imtg contracts already separate technical journal from user history | Shared contract plus optional history/journal services | M5: retention/deletion, redaction and authorization tests |

## Architectural decisions from the audit

### One router, two handler modes

M3 should extend the existing runtime boundary with a typed `UpdateRouter` and
declarative filters/middleware. It must schedule a handler on the existing
per-`ConversationKey` boundary and expose explicit `Accepted`, `QueueFull`,
`DeadlineMissed`, `Closed`, `Rejected` and `Failed` outcomes. A handler is either:

1. **interactive**, which may load a session and create a render intent; or
2. **headless**, which performs a domain command or schedules a domain effect
   without inventing a screen, UI tree or render intent.

This is an extension of the current dispatcher boundary, not a second generic
event framework. Background events use the same bounded scheduler and durable
job contract. Payment pre-checkout and join-request updates may use a bounded
deadline-aware lane within that scheduler, but still consume the same
admission, domain-key serialization, shutdown and quiescence accounting.
The urgent lane and its deadline/fairness policy are imtg design, not a claim
that aiogram supplies a generic handler-priority scheduler.

### Keep M2c-b `OutboxEffect` narrow

The current `OutboxEffect` remains a transport-neutral interaction effect for
the M2c-b migration. It must not gain speculative moderation or commerce enum
values. Local order transitions, inventory reservations, moderation decisions
and audit records are ordinary transactional domain writes; Telegram calls are
external effects with their own idempotency and UnknownOutcome rules.

M3 may define a typed adapter envelope for interaction operations (send, edit,
delete, callback answer) while preserving M2c-b logical slot and payload
identity. Moderation and commerce modules may own domain-specific outboxes or
typed commands. They must not reinterpret an old M2c-b effect row as a payment
or sanction, and no generic effect generalization is approved by this audit.

### Security and resource boundaries

All filters are bounded by input bytes, normalization work, queue capacity,
per-user/per-chat state and journal retention. Low-confidence moderation is
reversible or reviewable. AI/LLM analysis is optional, rate-limited and below
explicit administrator policy. Message content is not retained unless the
application's retention policy requires it.

Telegram Bot API permissions, privacy mode, join-request visibility, forum
topics and payment rules are adapter constraints. A bot cannot observe or
control platform events outside the updates and administrator rights granted
to it; no profile promises universal anti-raid protection.

## Rejected or deferred reuse

- Do not add aiogram, PTB, Go bot code, Lua plugins or Python shop code as a
  runtime dependency of this C++ library.
- Do not copy GPL, unlicensed or `NOASSERTION` source. Reimplement contracts
  from observed behavior and keep attribution/license review in the module.
- Do not add Mini Apps, AI moderation, analytics, CRM, or a universal inventory
  engine before a reference application demonstrates the need.
- Do not start MDBX or Telegram transport in this documentation PR.

## Review and acceptance gate

The next implementation review must demonstrate the three reference profiles
from `TELEGRAM_APPLICATION_PROFILES.md` against fake gateway/storage adapters,
then one minimal real Telegram example. The proof must include bounded
admission, restart/recovery, authorization, duplicate-update handling and
explicit UnknownOutcome behavior; a README-only integration is insufficient.
