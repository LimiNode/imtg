# ADR 0007: provider-neutral payment abstraction

**Status:** Proposed; implementation begins in M4/M5 after capability and
merchant-eligibility review.

## Context

The first Telegram shop target needs ordinary-individual checkout, Russian
cards including Mir, SBP, payment links, hosted checkout and webhooks. Crypto
and international providers are useful but secondary. Provider eligibility,
merchant contracts, payment methods and refund behavior differ by account and
jurisdiction, so a global hardcoded provider list would turn marketing claims
into unsafe runtime assumptions.

The existing imtg core deliberately has no provider SDK dependency. M2c-b
provides transport-neutral interaction effects and fenced recovery, but it is
not a commerce ledger. Payment state needs its own identities, revision/CAS
transitions, webhook authentication and `UnknownOutcome` handling.

## Decision

Create an optional `imtg_commerce` module with provider-neutral payment
contracts. Providers implement an `IPaymentProvider` boundary and register
their capabilities and merchant onboarding result in a `PaymentProviderRegistry`.
`PaymentService` selects a provider through a per-shop `PaymentPolicy`; it does
not contain provider-specific branches.

The module defines separate identities for orders, payments, attempts,
provider payments, refunds and fulfillment. Money uses exact integer minor units
and an explicit currency scale. A payment success URL is never authoritative;
only an authenticated, deduplicated provider event or a verified server-side
status response can move an attempt to `Succeeded`.

Telegram Stars is a separate provider path for permitted digital goods and
services. External providers are for policy-permitted physical goods/services;
the two paths share lifecycle and recovery rules but not transport semantics.

Payment acceptance and payout are separate capabilities. Provider inventory is
tracked with `candidate`, `needs_verification`, `eligible`, `deferred`,
`unsupported` and `rejected` states. Static provider capabilities are not
merchant onboarding eligibility. Eligibility is scoped to an exact merchant
account, merchant type, jurisdiction, product category, method and asset; its
evidence source and expiry are mandatory. No provider is production-eligible
until the capability audit, merchant onboarding and common conformance suite
pass.

Selection is fail-closed: `allowed_providers` is an explicit per-shop
allowlist, while `provider_priority` only orders providers inside that list.
An absent or expired eligibility decision, mismatched account or jurisdiction,
unsupported asset/method, and an unlisted provider all reject selection.

The commerce boundary distinguishes `DirectGateway`, `HostedCheckout`,
`SelfHostedCryptoProcessor`, `MarketplaceReseller` and `MerchantOfRecord`.
Only the first three fit the provider adapter boundary. Marketplace/reseller
and Merchant-of-Record integrations require separate contracts for catalog,
supplier/order ownership, fulfillment, refunds, customer support and legal
responsibility. A public partner disclosure or a storefront is not enough to
classify an integration as an open merchant API or Merchant of Record.

Crypto support is intentionally split between custodial/hosted gateways and
non-custodial self-hosted processors. A self-hosted option such as BTCPay
Server still needs explicit chain/Lightning settlement, wallet backup and
recovery policy; it does not remove the merchant's legal or operational
obligations.

For scarce inventory, the commerce module may create a short-lived,
transactional `InventoryHold` before a positive pre-checkout decision. The
hold is idempotent, fenced against competing orders, expires deterministically
and is revalidated after `successful_payment` before fulfillment reservation.

Webhook evidence is authenticated over the original bytes together with the
request path and headers. Payment state and event deduplication are committed
through one payment transaction with optimistic revisions; a failed
reconciliation must roll back rather than publish a partial transition. The
transaction reuses the storage `CommitResult` outcomes: an `Unknown` commit is
terminal until authoritative recovery and is never blindly retried.

## Consequences

Positive:

- a shop can choose FreeKassa, another eligible provider or Telegram Stars
  without changing imtg core;
- payment retries, duplicate webhooks, refunds and uncertain outcomes have one
  recovery model;
- provider claims remain auditable and account/jurisdiction-specific;
- provider SDKs stay in optional adapter targets and out of public core headers.

Costs and limits:

- each provider needs a capability audit, onboarding evidence and adapter tests;
- fallback cannot happen after an ambiguous attempt without reconciliation proof;
- payment contracts do not decide tax, receipt, prohibited goods or fulfillment
  policy;
- marketplace/reseller and Merchant-of-Record contracts add legal and
  operational responsibilities that a provider adapter cannot infer;
- the first implementation is still bounded by provider sandbox/live behavior.

## Rejected alternatives

1. **One universal provider enum in `imtg_core`:** couples the core to a
   changing vendor list and cannot express account-specific eligibility.
2. **Treat redirect success as payment:** unsafe because a browser redirect is
   not authenticated provider evidence.
3. **Use M2c-b `OutboxEffect` as a commerce ledger:** it loses payment
   identity, refund and webhook semantics and weakens recovery attribution.
4. **Automatic provider fallback after timeout:** may charge twice when the
   first provider accepted the request but its response was lost.
5. **Wrapping every payment page as `IPaymentProvider`:** hides seller,
   supplier, fulfillment and legal responsibility when the integration is a
   marketplace or Merchant of Record.

## Gates

Before the first production adapter:

1. the provider capability audit has exact official evidence and current
   merchant eligibility;
2. `PAYMENTS.md` conformance tests pass for idempotency, signatures, amount
   mismatch, status lookup, refunds and restart recovery;
3. payment state is committed before fulfillment and duplicate events are
   harmless;
4. inventory holds and expiry are race-safe for unique goods;
5. secrets, card data, replay protection, rate limits and `UnknownOutcome` are
   covered by the adapter review;
6. self-hosted crypto settlement, backup and restart recovery are tested when
   that model is selected;
7. Telegram policy separates Stars/digital goods from external physical-goods
   checkout.
