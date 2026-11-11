# Payment provider capability audit

**Status:** research baseline for the optional `imtg_commerce` module. **Audit
date:** 2026-11-11. This document is not an approval to onboard a provider and
does not claim that any payment adapter is implemented.

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
| Cryptomus | P2 | Merchant eligibility and jurisdiction require confirmation | Not the Russian-card/SBP path | Crypto payments | Merchant API/invoices and callbacks are advertised | `needs_verification` | Confirm merchant eligibility, KYC and dispute/refund behavior |
| PayKassa | P2 | Crypto gateway onboarding | Not the Russian-card/SBP path | Crypto payments | Gateway/API and callbacks are advertised | `claimed` | Verify current API, webhook signing and settlement finality |
| Plisio | P1 for crypto | Personal wallet access is advertised; commercial API eligibility for an ordinary individual needs confirmation | Not the Russian-card/SBP path | Crypto payments | Gateway/API and callbacks are advertised; verify refund and settlement semantics | `needs_verification` | Confirm merchant eligibility, KYC and sandbox/live behavior |
| BTCPay Server | P1 for crypto | Self-hosted deployment does not require registration with a hosted provider; merchant remains responsible for legal and operational eligibility | Not the Russian-card/SBP path | Bitcoin/Lightning and other configured processors | Self-hosted API and invoice/webhook surface; verify chain confirmations, Lightning settlement and recovery | `observed` for self-hosted architecture | Define node, wallet, confirmation, backup and incident-recovery policy before adapter work |
| CoinGate | P2 for crypto | Sole-trader onboarding may require tax documentation; anonymous personal merchant use is not assumed | Not the Russian-card/SBP path | Crypto payments | Hosted/API, callbacks and stated fee information require current account-level verification | `needs_verification` | Confirm merchant jurisdiction, KYC, refunds and payout constraints |
| Trybit (formerly CryptoCloud) | P1 research / P2 implementation | Public requirements describe connection for an individual without an IP; account moderation and production eligibility remain to be confirmed | Not the Russian-card/SBP path | Bitcoin, USDT, TON, Ethereum, Solana and other supported crypto assets | API V2 invoice creation, hosted checkout, signed POSTBACK and status lookup are documented; a standard refund API is not confirmed | `claimed` | Verify current API/authentication, POSTBACK signature, amount/network checks, invoice expiry, refund, AML/KYT, payout and multi-shop terms before the post-Mir/SBP adapter milestone |
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
- [Trybit](https://trybit.com/)
- [Trybit API documentation](https://docs.trybit.com/)
- [Trybit connection requirements](https://support.trybit.com/general-information/connection-requirements)
- [Trybit pricing](https://trybit.com/pricing)
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
| Trybit (formerly CryptoCloud) | [API documentation](https://docs.trybit.com/); [connection requirements](https://support.trybit.com/general-information/connection-requirements); [pricing](https://trybit.com/pricing) | Public pricing advertises a 1.9% base fee and individual tariffs from 0.4%; confirm the account-specific tariff, network fees, individual-account terms, API authentication, signed POSTBACK/JWT validation, status and amount/network checks, invoice expiry, refund model, AML/KYT holds, crypto payout, multi-shop and third-party merchant restrictions. |

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
decision. Plisio, BTCPay Server and Trybit are P1 crypto research candidates
with different custody and operating models; their adapters remain P2 work after
the main Mir/SBP path. CoinGate, CloudPayments and the other crypto providers
remain P2 or verification work. Trybit is a hosted/custodial crypto processor,
not a replacement for Mir or SBP, and its advertised individual onboarding does
not prove that it may collect funds for multiple independent shops. None of
these candidates changes the core payment lifecycle or the Russian-card/SBP
priority. Trybit's Telegram-bot API does not override Telegram Stars
requirements for digital goods or services sold inside Telegram; treat it as an
external crypto processor for permitted physical goods or services until the
applicable policy and merchant terms are verified.

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
