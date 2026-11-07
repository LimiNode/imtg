# Payment contract

**Semantic owner:** commerce/payment maintainers. **Status:** proposed,
contract-only. **Implementation:** the optional `imtg_commerce` module is not
part of `imtg_core` and is not implemented by this document.

This contract defines a provider-neutral payment boundary for Telegram shops
and other applications. It supports external hosted checkout, payment links,
provider APIs, webhooks, refunds and server-side status lookup without putting
provider SDK types or merchant policy in the core library.

## Boundaries

The payment module owns payment identity, provider selection, payment state,
webhook verification, reconciliation and refund intent. The application owns
orders, catalog, prices, inventory, fulfillment, tax/receipt policy and
merchant eligibility. The Telegram adapter owns update authentication and
transport. An external provider is never treated as an authority for local
order authorization without matching the order, amount, currency, merchant
account and payment attempt.

The first target is physical goods or services sold to ordinary individuals.
Telegram Stars remains a separate Telegram payment flow for permitted digital
goods and services. A Telegram `pre_checkout_query` approval is not a local
`Paid` transition; only an authoritative, deduplicated `successful_payment`
event can establish that state.

## Stable identities

The following identities are separate and must not be replaced by Telegram
callback data or a provider's mutable display number:

- `OrderId` — application order;
- `PaymentId` — one logical payment for the order;
- `PaymentAttemptId` — one attempt through one provider and method;
- `ProviderPaymentId` — provider-side identity, scoped by provider;
- `RefundId` — one refund request or provider refund;
- `FulfillmentId` — application delivery/reservation identity.

Every attempt stores its provider identity, method, amount, currency, merchant
account scope, immutable request digest and reconciliation context. A retry of
the same logical attempt keeps the same idempotency key. Switching provider is
a new attempt and is forbidden while the previous attempt is
`UnknownOutcome` unless policy explicitly establishes that the first attempt
cannot charge.

## Money and methods

Money is an exact integer amount in a declared currency/minor-unit scale. The
contract forbids `float` and `double` for payment arithmetic. Currency scale is
part of the currency policy and is validated before a provider request is
created. Rounding is explicit and never performed implicitly during provider
fallback.

Provider-neutral methods include `Card`, `Mir`, `Sbp`, `Crypto`,
`TelegramStars` and `Other`. A provider may expose more detail in its adapter,
but the core policy must not infer that a generic `Card` means `Mir`, or that an
SBP payout means SBP payment acceptance.

## Payment lifecycle

The common lifecycle is:

```text
Created
  -> Pending
  -> AwaitingConfirmation
  -> Succeeded
  -> Refunded / PartiallyRefunded
```

Terminal or uncertain branches are `Failed`, `Expired`, `Cancelled` and
`UnknownOutcome`. Provider-specific statuses are retained as evidence but are
mapped to the common state only when the semantic mapping is explicit.

Rules:

1. A success URL, browser redirect or client callback is not proof of payment.
2. A webhook is accepted only after authentication, merchant/account matching,
   amount and currency checks, and idempotency checks.
3. A server-side status query may reconcile `Pending`, `AwaitingConfirmation`
   and `UnknownOutcome`; it cannot turn an unauthenticated response into
   `Succeeded`.
4. `Succeeded` is committed before fulfillment can reserve or deliver goods.
5. A refund is a new auditable transition and never mutates the original
   payment identity.
6. An ambiguous provider result remains `UnknownOutcome`; automatic fallback
   must not create a second possible charge.

## Provider capabilities and eligibility

`PaymentProviderCapabilities` is declarative metadata, not a production
approval. It describes supported methods, hosted checkout, payment links,
direct API, webhooks, status lookup, refunds, partial refunds, sandbox and
whether the provider can expose a payment hold/capture boundary.

Eligibility is a separate policy result with the following states:

`candidate`, `needs_verification`, `eligible`, `deferred`, `unsupported` and
`rejected`.

The result records merchant type, jurisdiction, KYC/KYB outcome, prohibited
goods, reviewed evidence and expiry. A provider is selectable only when the
configured merchant account is `eligible` for the requested method and order
policy.

## Provider-neutral interfaces

The proposed public names are intentionally provider-neutral. They are
conceptual until an implementation PR freezes their exact C++ signatures.

```cpp
class IPaymentProvider;
class PaymentProviderRegistry;
class PaymentService;
class PaymentRepository;
class PaymentWebhookVerifier;
class PaymentReconciliation;
class PaymentPolicy;
class RefundService;
```

The implementation must provide these operations without exposing provider
SDK types:

- create an idempotent payment attempt and return a hosted URL, link or
  provider reference;
- query a provider status using the stored provider payment identity;
- verify a webhook over the original bytes and provider rules;
- reconcile an authenticated event or status response into the common
  lifecycle with expected-revision/CAS protection;
- request and reconcile full or partial refunds;
- list and select registered providers by declared capabilities and policy.

Every public operation returns an explicit success/error result. Network
timeouts, malformed provider responses and ambiguous commits are observable;
they are never converted into `Succeeded` by a default branch.

## Payment policy and provider selection

`PaymentPolicy` is evaluated per shop/order, not globally. It includes allowed
currencies and methods, merchant account, maximum amount, provider priority,
whether external checkout is permitted for the product, refund rules and the
fallback policy for `UnknownOutcome`.

Selection is deterministic: filter by merchant eligibility and method, then
apply configured priority and capability requirements. A provider failure does
not authorize fallback when the first attempt may have charged. Fallback is
allowed only after a terminal outcome or an explicit reconciliation proof that
the original attempt cannot charge.

## Webhook authentication and reconciliation

Webhook handling has its own authentication boundary; it is not Telegram
Update authentication. A verifier must:

1. validate the provider signature over the original request bytes and the
   provider's timestamp/replay rules;
2. validate merchant/account identity, payment/attempt identity, amount,
   currency and order binding;
3. deduplicate by provider event identity and immutable payload digest;
4. apply an atomic payment-state transition with the expected payment revision;
5. retain the authenticated evidence needed for later reconciliation.

Events may arrive late, duplicated or out of order. An older event cannot move
the state backwards. A contradictory event is recorded as an explicit
reconciliation conflict and requires policy or operator resolution.

## Inventory holds and fulfillment

For scarce or one-of-one inventory, a positive pre-checkout decision must not
promise stock without a short-lived transactional `InventoryHold`. The hold is
keyed by the order/payment attempt, has a bounded expiry, is idempotent, and
uses the same expected-revision/CAS fence as competing orders. It is released
on decline, cancellation or expiry.

`successful_payment` revalidates the hold before converting it into a durable
fulfillment reservation. If an authoritative payment has no available hold,
the commerce module records an explicit exception/refund or manual-recovery
state; it does not fabricate fulfillment or silently allocate the item twice.

## Repository and recovery contract

`PaymentRepository` persists payment, attempt, provider-event and refund
records behind the backend-neutral storage protocol. Writes use optimistic
expected revisions and are atomic with the local processed-update or webhook
deduplication record. Provider HTTP is outside the transaction.

The recovery journal must distinguish:

- request not admitted;
- request admitted but provider result unknown;
- authenticated provider success committed locally;
- local success committed but fulfillment still pending;
- refund requested, confirmed, unknown or failed.

Recovery may query a provider only through a registered adapter and must use
the original attempt identity and immutable request context. It never replays
an application callback or blindly creates a new charge.

## Security and data handling

Hosted checkout is preferred where it removes card-data handling from the
application. API keys and webhook secrets stay outside source control and are
never written to ordinary logs. Logs may contain stable internal IDs and
redacted provider evidence, but not card data, private keys or full payment
credentials.

Provider adapters must enforce bounded request/response sizes, timeouts,
replay protection, TLS verification and explicit rate limits. A provider's
merchant eligibility, receipt rules and jurisdiction are configuration and
onboarding inputs, not claims made by this library.

## Conformance gates

Every provider adapter must pass the same fake/sandbox conformance suite:

1. create-payment idempotency and immutable amount/currency checks;
2. duplicate, invalid-signature, replayed and out-of-order webhooks;
3. provider timeout, status lookup and `UnknownOutcome` recovery;
4. refund and partial-refund behavior where supported;
5. amount mismatch, merchant mismatch and unsupported-method rejection;
6. restart recovery without a duplicate charge;
7. scarce-inventory hold race and expiry behavior;
8. no fulfillment before authoritative `Succeeded`;
9. bounded payloads, redacted logs and secret handling;
10. provider selection and deterministic fallback policy.

The capability matrix in
[PAYMENT_PROVIDER_CAPABILITY_AUDIT.md](../research/PAYMENT_PROVIDER_CAPABILITY_AUDIT.md)
is the input to onboarding. It must be updated with exact evidence before a
provider is marked `eligible` or a production adapter is declared complete.
