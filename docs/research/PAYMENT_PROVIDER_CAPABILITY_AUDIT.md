# Payment provider capability audit

**Status:** research baseline for the optional `imtg_commerce` module. **Audit
date:** 2026-11-07. This document is not an approval to onboard a provider and
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

## Priority matrix

| Provider | Priority | Merchant/eligibility signal | Mir / SBP / cards | Crypto | Links, API, webhook, refund, status | Evidence level | Next gate |
|---|---:|---|---|---|---|---|---|
| FreeKassa | P0 | Merchant Checkout for individuals is advertised; exact onboarding and receipt rules must be checked | Mir and SBP methods are listed separately; confirm acceptance versus payout | Not a current requirement | Checkout/API and callback documentation are available; verify signature, status lookup and refund semantics | `claimed` | Build the first sandbox adapter only after live merchant eligibility and method tests |
| LAVA | P1 | Individuals/self-employed are described with commercial-activity conditions | Mir and SBP are advertised; confirm account and jurisdiction | Not a current priority | Hosted/API and callback details require account-level verification | `claimed` | Verify legal status, fees, limits, webhook signing and refund/status APIs |
| TabPay | P1 | Individual and self-employed onboarding is advertised | Mir and SBP are advertised | Not a current priority | Payment links/API/webhooks require operational verification | `needs_verification` | Confirm company/account checks, settlement behavior and sandbox/live parity |
| Paydex | P1 | Individual onboarding is advertised | Russian cards and SBP are advertised; Mir needs explicit confirmation | Not a current priority | Verify payment links, API, callbacks, status lookup and refunds | `needs_verification` | Confirm Mir acceptance and production reliability |
| AnyPay | P1 | Individual merchants are advertised | Mir/SBP acceptance must be distinguished from other card or payout features | Not a current priority | API/payment links and callbacks are advertised; verify signature and refunds | `needs_verification` | Run a method-by-method acceptance test with an eligible account |
| ENOT | P2 | The public API lists methods, but individual eligibility is not established here | Mir and SBP appear in API material; acceptance and merchant type need proof | Not a current priority | API/callback surface exists; verify current webhook and status contracts | `needs_verification` | Keep in inventory; do not select for P0 implementation |
| NOWPayments | P2 | Crypto merchant onboarding | Not the Russian-card/SBP path | Crypto payments | Hosted/API, webhooks, status and refunds depend on provider policy | `claimed` | Separate crypto adapter and compliance review |
| OxaPay | P2 | Crypto merchant onboarding | Not the Russian-card/SBP path | Crypto payments | Invoices, merchant API and callbacks are advertised | `claimed` | Verify webhook signatures, settlement and refund model |
| Cryptomus | P2 | Merchant eligibility and jurisdiction require confirmation | Not the Russian-card/SBP path | Crypto payments | Merchant API/invoices and callbacks are advertised | `needs_verification` | Confirm merchant eligibility, KYC and dispute/refund behavior |
| PayKassa | P2 | Crypto gateway onboarding | Not the Russian-card/SBP path | Crypto payments | Gateway/API and callbacks are advertised | `claimed` | Verify current API, webhook signing and settlement finality |
| FormaPay | P3 | Sufficient legal and operational conditions are not confirmed | Unknown | Unknown | Keep only as a research candidate until official evidence is collected | `deferred` | Do not implement or advertise support |
| Robokassa | P2 | Self-employed and other merchant paths require current onboarding review | Method availability is account and contract dependent | Not a current priority | Hosted checkout/API/callbacks are established patterns; verify current method and refund rules | `needs_verification` | Confirm individual/self-employed path and Russian method coverage |
| YooKassa | P2 | Merchant eligibility is contract-dependent | Method availability is account and contract dependent | Not a current priority | Hosted checkout/API/webhooks/status/refunds are established integration areas | `needs_verification` | Review current eligibility, receipt and jurisdiction constraints |
| Telegram Stars | P1 for digital Telegram goods | Telegram provider/account policy, not an external acquiring merchant | Not a Mir/SBP path | Telegram Stars | Telegram `pre_checkout_query` and `successful_payment` lifecycle | `observed` for the documented Telegram flow | Keep separate from external physical-goods checkout |

Official entry points:

- [FreeKassa](https://freekassa.net/)
- [LAVA](https://lava.ru/)
- [TabPay](https://tabpay.io/)
- [Paydex](https://paydex.ru/)
- [AnyPay](https://anypay.io/)
- [ENOT](https://enot.io/)
- [NOWPayments](https://nowpayments.io/)
- [OxaPay](https://oxapay.com/)
- [Cryptomus](https://cryptomus.com/)
- [PayKassa](https://paykassa.pro/)
- [FormaPay](https://formapay.com/)
- [Robokassa](https://robokassa.ru/)
- [YooKassa](https://yookassa.ru/)
- [Telegram Stars payments](https://core.telegram.org/bots/payments-stars)

## Capability checklist

Before a provider can become `eligible`, the audit must record:

1. official site and API documentation, with the exact page and review date;
2. accepted merchant types: individual, self-employed, individual
   entrepreneur, or company;
3. KYC/KYB, moderation, prohibited goods and jurisdiction restrictions;
4. separate evidence for Mir, SBP, other cards, crypto, payment links,
   hosted checkout, direct API and webhook delivery;
5. fees, limits, minimum amounts, settlement currency and payout rules;
6. webhook signature verification, idempotency, server-side status lookup and
   ordering/replay behavior;
7. sandbox availability and a test plan that uses no real money;
8. refund, dispute, chargeback and partial-refund semantics;
9. whether an inventory hold or delayed capture is possible for scarce goods;
10. unresolved questions, owner and the evidence needed to close them.

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
decision. Crypto providers are intentionally parallel P2 work and must not
change the core payment lifecycle or the Russian-card/SBP priority.

No provider is production-eligible merely because it appears in this inventory.
The first adapter must pass the common payment conformance suite and a
provider-specific eligibility review.
