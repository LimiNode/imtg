#ifndef IMTG_COMMERCE_PAYMENTS_HPP
#define IMTG_COMMERCE_PAYMENTS_HPP

#include "imtg/core/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imtg::commerce {

/// \brief Stable identifier for a registered payment provider.
struct ProviderId {
    std::string value; ///< Provider identifier.
    /// \brief Compares provider identities.
    friend bool operator==(const ProviderId& left, const ProviderId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Stable application order identity.
struct OrderId {
    std::string value; ///< Application-owned order identifier.
    /// \brief Compares order identities.
    friend bool operator==(const OrderId& left, const OrderId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Stable logical payment identity.
struct PaymentId {
    std::string value; ///< Application-owned payment identifier.
    /// \brief Compares payment identities.
    friend bool operator==(const PaymentId& left, const PaymentId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Stable identity for one provider payment attempt.
struct PaymentAttemptId {
    std::string value; ///< Application-owned attempt identifier.
    /// \brief Compares attempt identities.
    friend bool operator==(const PaymentAttemptId& left,
                           const PaymentAttemptId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Provider-side payment identity scoped by ProviderId.
struct ProviderPaymentId {
    std::string value; ///< Provider-side payment identifier.
    /// \brief Compares provider payment identities.
    friend bool operator==(const ProviderPaymentId& left,
                           const ProviderPaymentId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Stable refund identity.
struct RefundId {
    std::string value; ///< Provider or application refund identifier.
    /// \brief Compares refund identities.
    friend bool operator==(const RefundId& left, const RefundId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Stable fulfillment identity owned by the commerce application.
struct FulfillmentId {
    std::string value; ///< Application-owned fulfillment identifier.
    /// \brief Compares fulfillment identities.
    friend bool operator==(const FulfillmentId& left,
                           const FulfillmentId& right) noexcept
    { return left.value == right.value; }
};

/// \brief Exact minor-unit payment amount and currency code.
struct Money {
    std::int64_t minor_units{}; ///< Positive amount in currency minor units.
    std::string currency; ///< Three uppercase currency letters.

    /// \brief Creates and validates an exact money value.
    /// \param currency Three uppercase currency letters.
    /// \param minor_units Positive amount in minor units.
    /// \return Valid money or an invalid-argument error.
    [[nodiscard]] static Result<Money> from_minor_units(
        std::string_view currency, std::int64_t minor_units);

    /// \brief Compares amount and currency.
    friend bool operator==(const Money& left, const Money& right) noexcept
    { return left.minor_units == right.minor_units && left.currency == right.currency; }
};

/// \brief Provider-neutral payment method category.
enum class PaymentMethod : std::uint8_t {
    Card, Mir, Sbp, Crypto, TelegramStars, Other,
};

/// \brief Provider-neutral payment lifecycle state.
enum class PaymentStatus : std::uint8_t {
    Created, Pending, AwaitingConfirmation, Succeeded, Failed, Expired,
    Cancelled, UnknownOutcome, Refunded, PartiallyRefunded,
};

/// \brief Merchant account category used during provider eligibility checks.
enum class MerchantType : std::uint8_t {
    Individual, SelfEmployed, IndividualEntrepreneur, Company,
};

/// \brief Evidence state for one provider/account onboarding decision.
enum class PaymentEligibilityState : std::uint8_t {
    Candidate, NeedsVerification, Eligible, Deferred, Unsupported, Rejected,
};

/// \brief Declarative provider capabilities and onboarding evidence state.
struct PaymentProviderCapabilities {
    ProviderId provider; ///< Provider owning this record.
    PaymentEligibilityState eligibility = PaymentEligibilityState::NeedsVerification; ///< Onboarding evidence state.
    std::vector<PaymentMethod> methods; ///< Accepted payment methods.
    std::vector<MerchantType> merchant_types; ///< Advertised merchant categories.
    bool hosted_checkout = false; ///< Hosted checkout is available.
    bool payment_links = false; ///< Reusable payment links are available.
    bool direct_api = false; ///< Server-side create API is available.
    bool webhooks = false; ///< Authenticated webhooks are available.
    bool status_lookup = false; ///< Authoritative status lookup is available.
    bool refunds = false; ///< Refunds are supported.
    bool partial_refunds = false; ///< Partial refunds are supported.
    bool inventory_hold = false; ///< Capture/hold boundary is available.
    bool sandbox = false; ///< Non-production test environment is available.

    /// \brief Tests whether a method is listed in the capabilities.
    /// \param method Provider-neutral payment method.
    /// \return True when the method is supported.
    [[nodiscard]] bool supports_method(PaymentMethod method) const noexcept;
};

/// \brief Immutable request to create one idempotent payment attempt.
struct PaymentCreateRequest {
    PaymentAttemptId attempt_id; ///< Stable attempt identity.
    OrderId order_id; ///< Application order identity.
    Money amount; ///< Exact amount and currency.
    PaymentMethod method = PaymentMethod::Other; ///< Requested method.
    std::string idempotency_key; ///< Stable retry key for this attempt.
    std::string return_url; ///< Optional browser return URL.
};

/// \brief Provider-neutral result of payment creation.
struct PaymentCreateResult {
    ProviderPaymentId provider_payment; ///< Provider-side payment identity.
    PaymentStatus status = PaymentStatus::Pending; ///< Initial provider status.
    std::optional<std::string> checkout_url; ///< Hosted checkout or payment link.
};

/// \brief Authenticated provider status evidence used for reconciliation.
struct PaymentStatusSnapshot {
    ProviderPaymentId provider_payment; ///< Provider-side payment identity.
    PaymentStatus status = PaymentStatus::UnknownOutcome; ///< Mapped lifecycle state.
    Money amount; ///< Amount reported by the provider.
    std::string event_id; ///< Provider event identity, when supplied.
    std::string payload_digest; ///< Digest of authenticated raw payload.
};

/// \brief Immutable request to refund all or part of a payment.
struct RefundRequest {
    PaymentId payment_id; ///< Logical payment identity.
    PaymentAttemptId attempt_id; ///< Attempt being refunded.
    Money amount; ///< Positive full or partial refund amount.
    std::string idempotency_key; ///< Stable retry key for this refund.
};

/// \brief Provider-neutral result of a refund request.
struct RefundResult {
    RefundId refund_id; ///< Provider or application refund identity.
    PaymentStatus status = PaymentStatus::UnknownOutcome; ///< Refund outcome.
};

/// \brief Durable payment record shape used by a repository implementation.
struct PaymentRecord {
    PaymentId payment_id; ///< Logical payment identity.
    OrderId order_id; ///< Application order identity.
    PaymentAttemptId attempt_id; ///< Current provider attempt identity.
    ProviderId provider; ///< Provider selected for the attempt.
    ProviderPaymentId provider_payment; ///< Provider-side payment identity.
    Money amount; ///< Expected amount and currency.
    PaymentMethod method = PaymentMethod::Other; ///< Attempt method.
    PaymentStatus status = PaymentStatus::Created; ///< Current lifecycle state.
    std::string idempotency_key; ///< Immutable attempt idempotency key.
    std::uint64_t revision{}; ///< Repository optimistic revision.
};

/// \brief Per-shop constraints for deterministic provider selection.
struct PaymentPolicy {
    std::vector<PaymentMethod> allowed_methods; ///< Empty means all methods.
    std::vector<std::string> allowed_currencies; ///< Empty means any valid currency.
    std::vector<ProviderId> provider_priority; ///< Preferred provider order.
    bool allow_external_checkout = true; ///< Non-Telegram checkout is allowed.
    bool require_webhooks = true; ///< Provider must support authenticated webhooks.
    bool require_status_lookup = true; ///< Provider must support status lookup.
    bool require_refunds = true; ///< Provider must support refunds.

    /// \brief Tests whether a create request satisfies this policy.
    /// \param request Candidate payment request.
    /// \return True when local policy admits the request.
    [[nodiscard]] bool allows(const PaymentCreateRequest& request) const noexcept;
};

/// \brief Provider adapter boundary with no SDK types in the public contract.
class IPaymentProvider {
public:
    virtual ~IPaymentProvider() = default;
    /// \brief Returns the stable provider identity.
    /// \return Provider identity used for registry lookup and persisted records.
    [[nodiscard]] virtual ProviderId id() const noexcept = 0;
    /// \brief Returns declared capabilities and eligibility.
    /// \return Capability metadata; it must identify the same provider as `id()`.
    [[nodiscard]] virtual PaymentProviderCapabilities capabilities() const = 0;
    /// \brief Creates one idempotent provider payment attempt.
    /// \param request Immutable request identity, amount and method.
    /// \return Provider result or an error; an uncertain provider outcome must remain observable.
    [[nodiscard]] virtual Result<PaymentCreateResult> create_payment(
        const PaymentCreateRequest& request) = 0;
    /// \brief Queries authoritative provider status for an attempt.
    /// \param attempt_id Application attempt identity associated with the provider payment.
    /// \param provider_payment Provider-side payment identity.
    /// \return Authenticated status evidence or an error.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> query_status(
        const PaymentAttemptId& attempt_id,
        const ProviderPaymentId& provider_payment) = 0;
    /// \brief Requests an idempotent full or partial refund.
    /// \param request Refund identity, attempt identity, amount and retry key.
    /// \return Provider refund result or an error, including an uncertain outcome.
    [[nodiscard]] virtual Result<RefundResult> refund(
        const RefundRequest& request) = 0;
};

/// \brief Deterministic registry for independently configured providers.
class PaymentProviderRegistry final {
public:
    /// \brief Registers one provider and rejects duplicate or inconsistent IDs.
    /// \param provider Provider adapter whose lifetime is shared by the registry.
    /// \return Success, or an error when the adapter is null, duplicated or inconsistent.
    [[nodiscard]] Result<void> register_provider(
        std::shared_ptr<IPaymentProvider> provider);
    /// \brief Finds a provider by stable identity.
    /// \param provider Provider identity to find.
    /// \return The registered adapter, or null when no adapter matches.
    [[nodiscard]] std::shared_ptr<IPaymentProvider> find(
        const ProviderId& provider) const noexcept;
    /// \brief Selects the first eligible provider in policy order.
    /// \param request Validated payment request to satisfy.
    /// \param policy Per-shop method, currency, capability and priority policy.
    /// \return Selected adapter, or an error when policy or eligibility rejects all adapters.
    [[nodiscard]] Result<std::shared_ptr<IPaymentProvider>> select(
        const PaymentCreateRequest& request, const PaymentPolicy& policy) const;

private:
    std::vector<std::shared_ptr<IPaymentProvider>> m_providers;
};

/// \brief Backend-neutral repository boundary for durable payment records.
class PaymentRepository {
public:
    virtual ~PaymentRepository() = default;
    /// \brief Loads a payment from the repository snapshot.
    /// \param payment_id Logical payment identity to load.
    /// \return The record when present, an empty optional when absent, or an error.
    [[nodiscard]] virtual Result<std::optional<PaymentRecord>> load(
        const PaymentId& payment_id) const = 0;
    /// \brief Saves a record with optimistic expected-revision fencing.
    /// \param record Candidate payment record to save.
    /// \param expected_revision Revision observed by the caller; the repository rejects stale writes.
    /// \return Persisted record with its new revision, or an error.
    [[nodiscard]] virtual Result<PaymentRecord> save(
        const PaymentRecord& record, std::uint64_t expected_revision) = 0;
};

/// \brief Service boundary for payment creation and status lookup orchestration.
class PaymentService {
public:
    virtual ~PaymentService() = default;
    /// \brief Creates an attempt using deterministic provider selection.
    /// \param request Immutable application payment request.
    /// \param policy Per-shop provider and checkout policy.
    /// \return Provider creation result or an explicit error/unknown outcome.
    [[nodiscard]] virtual Result<PaymentCreateResult> create_payment(
        const PaymentCreateRequest& request, const PaymentPolicy& policy) = 0;
    /// \brief Queries status without treating a redirect as payment proof.
    /// \param attempt_id Application attempt identity.
    /// \param provider_payment Provider-side payment identity.
    /// \return Authenticated status evidence or an error.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> query_status(
        const PaymentAttemptId& attempt_id,
        const ProviderPaymentId& provider_payment) = 0;
};

/// \brief Authentication and decoding boundary for provider webhooks.
class PaymentWebhookVerifier {
public:
    virtual ~PaymentWebhookVerifier() = default;
    /// \brief Verifies original bytes and decodes authenticated evidence.
    /// \param raw_payload Original provider request bytes; they must not be normalized first.
    /// \param signature Provider signature or authentication header value.
    /// \return Authenticated status evidence, or an error for invalid/replayed input.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> verify_and_decode(
        std::string_view raw_payload, std::string_view signature) const = 0;
};

/// \brief Reconciliation boundary for authenticated provider evidence.
class PaymentReconciliation {
public:
    virtual ~PaymentReconciliation() = default;
    /// \brief Applies one snapshot with revision and idempotency rules.
    /// \param snapshot Authenticated provider evidence to apply.
    /// \param repository Repository used for the atomic revision-checked transition.
    /// \param payment_id Logical payment identity receiving the evidence.
    /// \return Updated payment record or an explicit conflict/error.
    [[nodiscard]] virtual Result<PaymentRecord> reconcile(
        const PaymentStatusSnapshot& snapshot, PaymentRepository& repository,
        const PaymentId& payment_id) = 0;
};

/// \brief Refund orchestration boundary with explicit uncertain outcomes.
class RefundService {
public:
    virtual ~RefundService() = default;
    /// \brief Requests a full or partial refund for an existing attempt.
    /// \param request Refund identity, amount and idempotency context.
    /// \return Refund result or an explicit error/unknown outcome.
    [[nodiscard]] virtual Result<RefundResult> request_refund(
        const RefundRequest& request) = 0;
};

/// \brief Validates a provider-neutral create request before network I/O.
[[nodiscard]] Result<void> validate_payment_request(
    const PaymentCreateRequest& request);

/// \brief Validates a refund request before network I/O.
[[nodiscard]] Result<void> validate_refund_request(
    const RefundRequest& request);

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENTS_HPP
