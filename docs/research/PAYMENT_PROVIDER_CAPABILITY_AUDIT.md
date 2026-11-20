# Payment provider capability audit

**Status:** research baseline for the optional `imtg_commerce` module. **Audit
date:** 2026-11-07. This document is not an approval to onboard a provider and
does not claim that any payment adapter is implemented.

**Addendum recorded:** 2026-11-20 for 2328.io, DonateX, DonatePay.eu and
MemeAlerts. Earlier entries retain their original review date. The addendum
distinguishes supplied research from pages independently inspected; neither
kind of documentation evidence establishes a working merchant integration.

## Scope and evidence rules

The first application target is a Telegram shop selling permitted physical
goods or services to ordinary individuals without requiring the customer to be
an individual entrepreneur. Russian cards, including Mir, and SBP are the
priority methods. Crypto and international methods are useful secondary
capabilities.

The URLs below are official provider or platform entry points to reproduce the
check. A provider's marketing statement is not proof of merchant eligibility,
payment acceptance, payout support, or production reliability. Each capability
must be rechecked during onboarding against the current merchant agreement,
jurisdiction, account type, limits, fees, sandbox and live behavior.

Use these evidence levels:

- `observed`: the official documentation exposes the capability and the
  reviewed account type is known;
- `claimed`: the provider advertises the capability, but the exact merchant
  or jurisdiction conditions still need confirmation;
- `needs_verification`: the capability or eligibility is ambiguous;
- `deferred`: useful to keep in the inventory, but not a current implementation
  target;
- `rejected`: the current evidence is insufficient or contradicts the required
  policy.

Payment acceptance and withdrawal are separate capabilities. In particular,
an SBP payout API does not prove that the provider accepts an SBP payment from
the buyer. A provider that accepts a private individual as a merchant still
does not remove tax, receipt, KYC/KYB, consumer-protection or jurisdiction
obligations.

## Commercial integration models

The inventory distinguishes the commercial role of an integration. These
models are not interchangeable and must not all be implemented as an
`IPaymentProvider`:

- `DirectGateway`: the shop remains the seller and a provider processes the
  payment for the shop's merchant account;
- `HostedCheckout`: the shop remains the seller, but payment data and the
  checkout surface are hosted by the provider;
- `SelfHostedCryptoProcessor`: the shop operates a processor such as BTCPay
  Server and controls its own wallet, node and recovery policy;
- `MarketplaceReseller`: a platform sells or resells third-party offers and
  owns supplier, catalog, order and fulfillment relationships;
- `MerchantOfRecord`: a third party is contractually the seller and assumes
  the applicable settlement, tax, receipt and consumer obligations.

Evidence that a platform is a reseller or partner does not prove that it
offers an open merchant API or acts as a Merchant of Record. The latter two
models require separate commerce-integration contracts and explicit legal
and operational review.

## Priority matrix

| Provider | Priority | Merchant/eligibility signal | Mir / SBP / cards | Crypto | Links, API, webhook, refund, status | Evidence level | Next gate |
|---|---:|---|---|---|---|---|---|
| FreeKassa | P0 | Merchant Checkout for individuals is advertised; exact onboarding and receipt rules must be checked | Mir and SBP methods are listed separately; confirm acceptance versus payout | Not a current requirement | Checkout/API and callback documentation are available; verify signature, status lookup and refund semantics | `claimed` | Build the first sandbox adapter only after live merchant eligibility and method tests |
| LAVA | P1 | Individuals/self-employed are described with commercial-activity conditions; HideMy.name publicly identifies LAVA as a Russian payment partner, which is partner evidence only | Mir and SBP are advertised; confirm account and jurisdiction | Not a current priority | Hosted/API and callback details require account-level verification | `claimed` | Verify current legal entity, merchant terms, fees, limits, webhook signing and refund/status APIs |
| TabPay | P1 | Individual and self-employed onboarding is advertised; contractual restrictions below are unresolved | Mir and SBP are advertised | Not a current priority | Payment links/API/webhooks require operational verification | `needs_verification` | Confirm the legal entity, account model, aggregation/routing restrictions, settlement behavior and sandbox/live parity |
| Paydex | P1 | Individual onboarding is advertised; canonical source is `paydex.pro` and operator attribution is not inferred from another domain | Russian cards and SBP are advertised; Mir needs explicit confirmation | Not a current priority | Verify payment links, API, callbacks, status lookup and refunds | `needs_verification` | Confirm legal entity, Mir acceptance and production reliability |
| AnyPay | P1 | Individual merchants are advertised | Mir/SBP acceptance must be distinguished from other card or payout features | Not a current priority | API/payment links and callbacks are advertised; verify signature and refunds | `needs_verification` | Run a method-by-method acceptance test with an eligible account |
| ENOT | P2 | The public API lists methods, but individual eligibility is not established here | Mir and SBP appear in API material; acceptance and merchant type need proof | Not a current priority | API/callback surface exists; verify current webhook and status contracts | `needs_verification` | Keep in inventory; do not select for P0 implementation |
| NOWPayments | P2 | Crypto merchant onboarding | Not the Russian-card/SBP path | Crypto payments | Hosted/API, webhooks, status and refunds depend on provider policy | `claimed` | Separate crypto adapter and compliance review |
| OxaPay | P2 | Crypto merchant onboarding | Not the Russian-card/SBP path | Crypto payments | Invoices, merchant API and callbacks are advertised | `claimed` | Verify webhook signatures, settlement and refund model |
| [2328.io](#2328io-crypto-processing-candidate) | P1 research for crypto | Supplied research describes individual merchants, subject to jurisdiction, onboarding and KYC | Mir/SBP acquiring is not confirmed; a RUB invoice price is not RUB payment acceptance | USDT, USDC, BTC, ETH, TON and other assets/networks are reported; verify the exact account route | Reported hosted/API invoices, project-scoped `order_id` idempotency, HMAC webhooks, status lookup and crypto payouts; refund API and full sandbox unconfirmed | `needs_verification` | Verify current API/terms, operator, account eligibility, immutable retry semantics, refund process and a controlled payment/refund/payout cycle |
| Cryptomus | P2 | Merchant eligibility and jurisdiction require confirmation | Not the Russian-card/SBP path | Crypto payments | Merchant API/invoices and callbacks are advertised | `needs_verification` | Confirm merchant eligibility, KYC and dispute/refund behavior |
| PayKassa | P2 | Crypto gateway onboarding | Not the Russian-card/SBP path | Crypto payments | Gateway/API and callbacks are advertised | `claimed` | Verify current API, webhook signing and settlement finality |
| Plisio | P1 for crypto | Personal wallet access is advertised; commercial API eligibility for an ordinary individual needs confirmation | Not the Russian-card/SBP path | Crypto payments | Gateway/API and callbacks are advertised; verify refund and settlement semantics | `needs_verification` | Confirm merchant eligibility, KYC and sandbox/live behavior |
| BTCPay Server | P1 for crypto | Self-hosted deployment does not require registration with a hosted provider; merchant remains responsible for legal and operational eligibility | Not the Russian-card/SBP path | Bitcoin/Lightning and other configured processors | Self-hosted API and invoice/webhook surface; verify chain confirmations, Lightning settlement and recovery | `observed` for self-hosted architecture | Define node, wallet, confirmation, backup and incident-recovery policy before adapter work |
| CoinGate | P2 for crypto | Sole-trader onboarding may require tax documentation; anonymous personal merchant use is not assumed | Not the Russian-card/SBP path | Crypto payments | Hosted/API, callbacks and stated fee information require current account-level verification | `needs_verification` | Confirm merchant jurisdiction, KYC, refunds and payout constraints |
| CloudPayments | P2 | Self-employed onboarding is advertised; ordinary-individual eligibility is not confirmed | Mir, Mir Pay and SBP are advertised; confirm acceptance, recurring-payment and payout conditions | Not a current priority | Hosted/API, recurring payments and callbacks require current contract verification | `needs_verification` | Confirm merchant type, receipt rules, limits, webhook signing and refunds |
| FormaPay | P3 | Sufficient legal and operational conditions are not confirmed; canonical source recorded as `formapay.io` | Unknown | Unknown | Keep only as a research candidate until official evidence is collected | `deferred` | Do not implement or advertise support |
| Robokassa | P2 | Self-employed and other merchant paths require current onboarding review | Method availability is account and contract dependent | Not a current priority | Hosted checkout/API/callbacks are established patterns; verify current method and refund rules | `needs_verification` | Confirm individual/self-employed path and Russian method coverage |
| YooKassa | P2 | Merchant eligibility is contract-dependent | Method availability is account and contract dependent | Not a current priority | Hosted checkout/API/webhooks/status/refunds are established integration areas | `needs_verification` | Review current eligibility, receipt and jurisdiction constraints |
| Telegram Stars | P1 for digital Telegram goods | Telegram provider/account policy, not an external acquiring merchant | Not a Mir/SBP path | Telegram Stars | Telegram `pre_checkout_query` and `successful_payment` lifecycle | `observed` for the documented Telegram flow | Keep separate from external physical-goods checkout |

## Commercial platform candidates

These entries are intentionally separate from the payment-provider matrix:

| Platform | Priority | Model under review | Evidence level | Required gate |
|---|---:|---|---|---|
| VPNPay | P2 research | `MarketplaceReseller`; possibly a platform partnership, but not yet an open merchant API or Merchant of Record | `needs_verification` | Confirm supplier contract, API/affiliate boundary, seller-of-record allocation, refunds, fulfillment and Telegram product-policy compatibility |

HideMy.name is a useful evidence source for the LAVA partner relationship, not
a payment-provider candidate for the inventory. A successful storefront or
partner disclosure must not be promoted into a generic adapter capability.

### LAVA partner evidence

The supplied review records that HideMy.name names ООО «ЛАВ-А» as its Russian
payment partner and publishes INN `9702037833`; the LAVA legal document cited
in that review shows the same INN and OGRN `1217700529023`. This is useful
evidence that one service negotiated a LAVA relationship, but it is not a
guarantee of equivalent terms for another merchant. Legal-entity details may
change and must be rechecked against the current provider agreement and
official registry before onboarding.

Official entry points:

- [FreeKassa](https://freekassa.net/)
- [LAVA](https://lava.ru/)
- [TabPay](https://tabpay.org/terms/)
- [Paydex](https://paydex.pro/)
- [AnyPay](https://anypay.io/)
- [ENOT](https://enot.io/)
- [NOWPayments](https://nowpayments.io/)
- [OxaPay](https://oxapay.com/)
- [Cryptomus](https://cryptomus.com/)
- [PayKassa](https://paykassa.pro/)
- [Plisio](https://plisio.net/)
- [BTCPay Server](https://btcpayserver.org/)
- [CoinGate](https://coingate.com/)
- [CloudPayments](https://cloudpayments.ru/)
- [FormaPay](https://formapay.io/)
- [Robokassa](https://robokassa.ru/)
- [YooKassa](https://yookassa.ru/)
- [Telegram Stars payments](https://core.telegram.org/bots/payments-stars)
- [HideMy.name payment-partner disclosure](https://hide-my-name.cc/)
- [VPNPay](https://vpnpay.io/)

The HideMy.name disclosure is evidence of one commercial relationship with
LAVA, not evidence that LAVA offers the same onboarding terms to every
merchant. VPNPay is recorded as a marketplace/reseller research candidate; its
public storefront does not by itself establish an open merchant API or
Merchant-of-Record status for third-party Telegram shops.

## Evidence register and unresolved provider terms

The following is a source register, not a claim that a capability has been
observed. The date is the date this audit recorded the URL (`2026-11-07`), not
the publication date of the provider document. Homepage-only material remains
`claimed` or `needs_verification`; it cannot establish merchant eligibility,
Mir/SBP acceptance, webhook signatures, status lookup or settlement.

| Provider | Exact source recorded | Evidence that still must be captured before `eligible` |
|---|---|---|
| FreeKassa | [official entry point](https://freekassa.net/) | Merchant eligibility and public offer for the exact account; separate Mir and SBP acceptance; API/webhook signature; status lookup; refund; settlement/payout. |
| LAVA | [official entry point](https://lava.ru/); [partner disclosure](https://hide-my-name.cc/) | Current legal entity and offer; account/jurisdiction eligibility; method acceptance; API/webhook signature; status/refund; settlement. The partner disclosure is not transferable onboarding evidence. |
| TabPay | [terms](https://tabpay.org/terms/) | Public offer and API must be checked together with the exact account. Terms appear to address payment aggregation/routing, accepting payments for third parties, third-party resources or API credentials, suspension/holds up to 180 days, and a 100% penalty with a 100,000 RUB minimum; quote and applicability require legal verification. |
| Paydex | [canonical entry point](https://paydex.pro/) | Merchant eligibility, public offer, API, Mir/SBP acceptance, webhook/signature, status, refund and settlement evidence. `paydex.ru` is not treated as the canonical source and no common operator is assumed. |
| AnyPay | [official entry point](https://anypay.io/) | Merchant eligibility, public offer, method-by-method Mir/SBP acceptance, API/webhook signature, status, refund and settlement evidence. |

TabPay's terms also make multi-shop, multi-provider and platform use a
`needs_verification` case. The audit must not treat a REST API or payment page
as permission for any of the following without a provider decision:

1. one merchant selling its own goods or services;
2. one operator running several of its own shops;
3. a platform where third-party merchants connect their own accounts;
4. a platform collecting funds for other sellers.

These scenarios have different seller-of-record, KYC/KYB, settlement,
chargeback, tax and contractual consequences. Provider domains are recorded
independently: `tabpay.org`, `paydex.pro` and `formapay.io` must not be
attributed to one operator without explicit evidence.

## 2328.io crypto processing candidate

**Recorded:** 2026-11-20. **Priority:** P1 research for an optional crypto
adapter, not a production selection. The supplied research cites the
[official site](https://2328.io/ru/) and [API documentation](https://2328.io/api).
Both URLs returned HTTP 403 to the unauthenticated documentation fetch in this
update. The following details are therefore recorded from that research, not
claimed as independently reverified API or legal facts.

| Area | Reported capability | Gate before implementation or eligibility |
|---|---|---|
| Payment creation | `POST /v1/payment`, hosted checkout and Telegram Mini App deeplink; prices may be denominated in RUB/USD/EUR while the buyer pays crypto | Verify asset/network, quote/settlement currency, expiry and checkout ownership; do not infer Mir, SBP or fiat acquiring |
| Request authentication | Project UUID and HMAC-SHA256 at `https://api.2328.io/api`; 10 requests/second per project reported | Capture exact signing/encoding rules, rate-limit behavior and immutable signature vectors; secrets stay outside source/logs |
| Attempt identity | Repeated creation with the same project-scoped `order_id` reportedly returns the existing invoice; `POST /v1/payment/info` accepts `uuid` or `order_id` | Map `order_id` to one `PaymentAttemptId` within the merchant project, not a reusable application order ID; verify key limits, retention and conflicting-payload behavior |
| Notifications/history | HMAC-SHA256 webhook with `sign` in the JSON object; `POST /v1/payment/list` and `POST /v1/test-webhook/payment` reported | Preserve original bytes and implement the provider's exact canonicalization rules with known-answer vectors, replay and duplicate tests; a synthetic signed event is not a payment |
| Payout/refund | `POST /v1/payout` for crypto; refund policy/statuses reported, but no public refund initiation API confirmed | Separate payout from refund and bank settlement; leave `refunds`/`partial_refunds` false for an adapter until its supported refund workflow is verified |
| Test environment | Signed synthetic webhook testing reported; no full payment sandbox confirmed | Do not set `sandbox=true` from webhook tests or infer settlement/withdrawal success; real-money tests require separate authorization |

Documented idempotency, if verified, is scoped to the original attempt and
immutable amount/asset/account context. A timeout remains `UnknownOutcome`
until authoritative lookup resolves that attempt; it does not authorize a new
`order_id`, provider fallback or a blind retry with changed fields. The existing
[payment contract](../contracts/PAYMENTS.md) remains authoritative.

The research lists `awaiting_confirmation`, `underpaid_check`, `underpaid`,
`overpaid`, `aml_lock`, `cancel` and refund states. Preserve provider evidence
and freeze the exact state mapping before an adapter is implemented:

- confirmations still pending must not authorize fulfillment;
- underpayment, overpayment and AML locks need explicit reconciliation or
  operator policy, not a default success branch;
- even `paid` requires authenticated account, attempt, amount and asset
  matching before local `Succeeded`;
- payout and refund status are not interchangeable with payment success.

The current `PaymentPolicy::require_refunds` default must not be weakened to
admit this candidate. A refund policy page or payout endpoint alone does not
prove that a future adapter can honor the required refund operation.

### 2328.io commercial and legal gates

The supplied research describes an individual-merchant path for selling one's
own goods/services, subject to provider approval. It reports Level 1 identity
checks and Level 2 verification, potentially including Sumsub/biometrics, after
aggregate completed withdrawals equivalent to USD 1,000; risk checks may occur
earlier. Confirm the current threshold, residency/product eligibility and data
handling in the exact account agreement. This is not a no-KYC or no-tax claim.

It attributes the operator to `3-102-115916 SOCIEDAD DE RESPONSABILIDAD LIMITADA`,
trading as Processing Engines in Costa Rica, with a reported registration date
of 2026-10-22. No official registry entry was independently verified. Treat
operator identity, licensing/contract scope, custody, settlement times and
account holds as unresolved onboarding evidence. The advertised fee from 0.3%
is a claim, not an all-in quote or reliability guarantee.

The research also reports a restriction on accepting payments for undisclosed
third-party merchants without written approval. A PayWeaver multi-merchant
deployment therefore needs explicit platform approval, seller/account
separation and contractual responsibility; one shared merchant account is not
assumed permissible. No PayWeaver repository or connector is changed here.

Before implementation, the commerce/payment maintainers must obtain current
API and legal pages plus written answers about jurisdictions, refund API or
supported manual process, fees, payout timing, sandbox and multi-shop use.
Verify signing/idempotency with offline fixtures first. Account registration,
real payments/refunds/payouts and provider contact require separate authority.
External crypto checkout does not replace Telegram Stars for Telegram digital
goods/services.

## External donation platforms

**Recorded:** 2026-11-20. These are optional application-event research
candidates, not additional order-payment gateways. Public API documentation
does not establish merchant eligibility, settlement, refund support or an
authoritative receipt for an imtg order. No account, OAuth application or
connector was created, and no authenticated API or real-money test was run.

| Platform | Priority for event-source research | Documented or reported integration surface | Commercial/implementation gate |
|---|---|---|---|
| DonateX | P1, first read-only candidate | Official docs describe an External Token for one's own account, OAuth 2.0 with PKCE for multi-user applications, donation history, SignalR and signed webhooks | `needs_verification`: approved application/account, scopes, history retention/gaps and webhook fixtures; do not infer an arbitrary merchant create-payment/refund API |
| DonatePay.eu | P2 | API-token donation reading is reported in historical integrations, often for donatepay.ru; the current donatepay.eu API contract is unconfirmed | `needs_verification`: current official endpoint, authentication, replay, event identity and terms; no guessed protocol or compatibility with another domain |
| MemeAlerts | P1 for interactive events, after API access approval | Official developer document describes OAuth 2.0, REST and Centrifugo v5; donation, virtual-coin and interactive events are distinct | `needs_verification`: account/application access, channel scopes, token lifecycle, source IDs and cross-channel deduplication; not a general checkout provider |

### DonateX

The official [authentication](https://donatex.gg/api-docs/authentication),
[donation](https://donatex.gg/api-docs/donations),
[realtime](https://donatex.gg/api-docs/realtime) and
[webhook](https://donatex.gg/api-docs/webhooks) pages were accessible without
credentials in this update. They document:

- External Token access to the owner's full account without refresh/scopes;
  multi-user applications use OAuth 2.0 Authorization Code with mandatory
  PKCE. Access tokens last one hour; `offline_access` enables rotating refresh
  tokens. Save each new pair atomically, scoped to the original account.
- `GET /api/v1/donations` has paginated history/date filters and `hideTest`
  defaults to false. `POST /api/v1/test-donation` creates synthetic events in
  the normal alert, realtime and webhook paths. Check the donation's `isTest`;
  do not count all `DonationCreated` events as real financial activity.
- SignalR emits `DonationCreated`, `DonationRerun` and `GoalUpdated`. Rerun also
  covers moderation approval; recover missed donations from history and
  deduplicate by donation `id`, including Created/Rerun overlap.
- Webhooks currently expose `DonationCreated`, not every realtime event.
  `X-DonateX-Signature` is lowercase hexadecimal HMAC-SHA256 over raw body
  bytes; compare in constant time, without JSON reserialization. The
  subscription ID identifies a subscription, not the underlying donation.
- Webhooks expect 2xx within 10 seconds, retry up to three times and disable a
  subscription after five consecutive failed attempts, including retries.

Freeze schemas, signature/replay vectors, history-retention/gap behavior and
subscription-health recovery before implementation. A durable event inbox
should acknowledge verified intake promptly and handle reactions separately;
an alert rerun or a test donation is not a new financial event.

The [official site](https://donatex.gg/) advertises Mir, Visa/Mastercard, SBP
and SberPay with fees from 6% according to the supplied research. These are
platform/account claims, not selectable `PaymentMethod` capabilities in imtg.
Confirm the exact beneficiary type, jurisdiction, KYC, payout schedule,
fees/refunds and third-party collection restrictions for the intended account.

### DonatePay.eu

The [terms](https://donatepay.eu/terms) were accessible in this update and
displayed "Last updated: June 2, 2025". They include individual KYC provisions
and a RUB 100/calendar-day maintenance charge starting the day after assignment
of Inactive Streamer status, with multiple inactivity triggers and restrictive
balance provisions. Applicability requires current account/legal review;
these published terms do not establish the current API.
Keep [donatepay.eu](https://donatepay.eu/)
separate from `donatepay.ru`, `donpay.app` and `donationpay.org`. No common
operator, credential or endpoint compatibility is assumed.

The supplied research reports card/bank and crypto routes, RUB/USD/EUR/UAH/USDT
options and advertised withdrawal from RUB 50. These are not universal account
guarantees: capture the applicable method, fee, refund/withdrawal and operator
agreement before registration or selection. A frontend API-client asset is not
a public integration specification. Read-only integration remains blocked until
a current official API and credential contract for this domain is known.

### MemeAlerts

The [developer entry point](https://memealerts.com/developers/api) loads a
public [API document](https://memealerts.com/api/v1/docs). That document was
accessible without credentials in this update and describes OAuth 2.0
Authorization Code and Centrifugo v5 at
`wss://memealerts.com/connection/websocket`. It specifies one-hour access tokens,
24-hour connection/subscription tokens and rotating refresh tokens. Refresh
must be serialized per account; token lifetimes/grace windows are provider
rules to recheck, not hardcoded library defaults.

The document describes a 60-second refresh grace period, fresh subscription
tokens on reconnect and omission of channels without the required scopes.
Coordinate refresh across processes/channels, and treat missing channels as
an explicit authorization/capability result rather than a silent successful
subscription. Historical replay/retention guarantees and a synthetic-event
facility are not established here; do not invent a cursor or `isTest` field.

The document distinguishes `donation` from `coin_purchase`; a donation may be
fiat or virtual coins. The same donation can appear on both its dedicated
channel and the general event channel. Deduplicate by original account/event
identity, not transport channel, and never interpret virtual coins as fiat
revenue. Delivery may follow premoderation and is not a bank-settlement clock.

The supplied research reports third-party payment processing, an agency model
and a contractual commission ceiling of 25%. Confirm the current applicable
agreement, actual rate, payout/refund/KYC rules, API access approval and rights
to receive third-party events before implementation. A ceiling is not a quote,
and purchase of virtual coins is not proof of a shop order payment.

### Connector boundary and acceptance gates

These are future acceptance requirements, not a new public interface or an
implemented event subsystem:

1. Bind credentials and records to the exact source/account. Keep the underlying
   donation identity separate from a delivery/rerun event identity, using
   provider-issued IDs where available and documenting any missing-ID gate.
   Deduplicate donations across channels/reruns by original donation identity
   and a versioned, normalized fingerprint of immutable financial attributes.
   Do not compare full transport-payload hashes across envelopes: delivery
   timestamps, `wasShown` and channel/presentation fields may legitimately
   change. Retain raw bytes/digests for authentication/audit separately.
   Conflicting immutable financial attributes require explicit reconciliation,
   not a new donation; neither mutable envelopes nor account reuse may bypass
   durable deduplication.
2. Authenticate webhooks/realtime subscriptions and persist accepted events
   before acknowledging delivery. Define bounded queues, payload/retention
   limits, reconnect cursors, history replay and unrecoverable gaps. Reuse
   existing storage, delivery and application-event contracts rather than a
   second orchestration system.
3. Distinguish real donations, synthetic/test events, alert reruns, goals and
   virtual-coin purchases. Tests/reruns must not inflate financial reports;
   no external event automatically creates local `Succeeded` or fulfillment.
4. Treat messages/media URLs as untrusted; limit or reject media fetches,
   escape presentation content and keep credentials/private payloads out of
   ordinary logs. Scope authorization and token rotation to the exact account.
5. Require offline tests for duplicates across differing channel/rerun envelopes,
   conflicting immutable financial attributes, missing/malformed fields, invalid
   authentication, expired/rotated tokens, reconnect/replay, bounded resources
   and separation from the order-payment ledger. Do not invent a transport or
   API solely to make a proposed connector appear implemented.

Candidate order is DonateX read-only intake, then MemeAlerts after access
confirmation, then DonatePay.eu after its API is verified. All remain optional
and outside `imtg_core`; they do not block M3 or the first beta. PayWeaver
connectors, OBS delivery and PayWeaver architecture changes require a separate
task in that project, not changes to imtg's generic payment interfaces.

## Capability checklist

Before a provider can become `eligible`, the audit must record:

1. official site and API documentation, with the exact page and review date;
2. accepted merchant types: individual, self-employed, individual
   entrepreneur, or company;
3. commercial integration model, registration/residency country and any
   sanctions or cross-border constraints;
4. KYC/KYB, moderation, prohibited goods and jurisdiction restrictions;
5. separate evidence for Mir, SBP, other cards, crypto, payment links,
   hosted checkout, direct API and webhook delivery;
6. fees, limits, minimum amounts, settlement currency and payout rules;
7. webhook signature verification, idempotency, server-side status lookup and
   ordering/replay behavior;
8. sandbox availability and a test plan that uses no real money;
9. refund, dispute, chargeback and partial-refund semantics;
10. whether an inventory hold or delayed capture is possible for scarce goods;
11. unresolved questions, owner and the evidence needed to close them.

The audit must distinguish:

- a statement on the provider's website;
- a condition in the user agreement;
- a documented API operation;
- successful onboarding;
- a reproducible sandbox or live test.

## Prioritized implementation order

FreeKassa is the P0 prototype candidate because the supplied research points to
an individual-friendly checkout and the required Russian methods. It remains a
candidate until eligibility, receipt, webhook, refund and Mir/SBP acceptance are
confirmed in a sandbox and with an approved merchant account.

LAVA, TabPay, Paydex and AnyPay are independent P1 candidates. They must not be
treated as interchangeable: each needs its own capability record and onboarding
decision. Plisio and BTCPay Server are parallel P1 crypto candidates with
different custody and operating models; CoinGate, CloudPayments and the other
crypto providers remain P2 or verification work. None of them changes the core
payment lifecycle or the Russian-card/SBP priority.

YooKassa's documented non-resident path is a separate company-and-contract
scenario, not a substitute for the ordinary-individual-without-IP target. It
requires an eligible foreign company, corporate identification and supported
settlement conditions; SBP availability for non-residents must not be assumed.

The audit does not record a blanket European-Union prohibition for NOWPayments
without an explicit current source. Jurisdiction, sanctions and payment-method
restrictions remain account- and route-specific compliance gates. Likewise,
successful VPN sales on a separate marketplace do not prove that an external
checkout for a digital service is permitted inside a Telegram bot; Telegram
Stars policy remains a separate gate.

No provider is production-eligible merely because it appears in this inventory.
The first adapter must pass the common payment conformance suite and a
provider-specific eligibility review.
