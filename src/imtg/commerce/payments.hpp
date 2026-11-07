#ifndef IMTG_COMMERCE_PAYMENTS_HPP
#define IMTG_COMMERCE_PAYMENTS_HPP

#include "imtg/core/clock.hpp"
#include "imtg/core/result.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace imtg::commerce {

/// \brief Stable provider identity.
struct ProviderId {
    std::string value; ///< Provider identifier.
    /// \brief Compares provider identities.
    friend bool operator==(const ProviderId& a, const ProviderId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable merchant-account identity.
struct MerchantAccountId {
    std::string value; ///< Provider or application account identifier.
    /// \brief Compares merchant-account identities.
    friend bool operator==(const MerchantAccountId& a,
                           const MerchantAccountId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable order identity.
struct OrderId {
    std::string value; ///< Application order identifier.
    /// \brief Compares order identities.
    friend bool operator==(const OrderId& a, const OrderId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable logical payment identity.
struct PaymentId {
    std::string value; ///< Application payment identifier.
    /// \brief Compares payment identities.
    friend bool operator==(const PaymentId& a, const PaymentId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable payment-attempt identity.
struct PaymentAttemptId {
    std::string value; ///< Application attempt identifier.
    /// \brief Compares payment-attempt identities.
    friend bool operator==(const PaymentAttemptId& a,
                           const PaymentAttemptId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Provider payment identity scoped by provider.
struct ProviderPaymentId {
    std::string value; ///< Provider-side payment identifier.
    /// \brief Compares provider-payment identities.
    friend bool operator==(const ProviderPaymentId& a,
                           const ProviderPaymentId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable refund identity.
struct RefundId {
    std::string value; ///< Refund identifier.
    /// \brief Compares refund identities.
    friend bool operator==(const RefundId& a, const RefundId& b) noexcept
    { return a.value == b.value; }
};
/// \brief Stable fulfillment identity.
struct FulfillmentId {
    std::string value; ///< Fulfillment identifier.
    /// \brief Compares fulfillment identities.
    friend bool operator==(const FulfillmentId& a,
                           const FulfillmentId& b) noexcept
    { return a.value == b.value; }
};

/// \brief Fiat or network-specific crypto asset kind.
enum class AssetKind : std::uint8_t { Unknown, Fiat, Crypto };
/// \brief Explicit asset identity.
struct AssetId {
    AssetKind kind = AssetKind::Unknown; ///< Asset kind.
    std::string code; ///< Fiat code or crypto symbol.
    std::string network; ///< Crypto network discriminator.
    /// \brief Compares exact asset identities.
    friend bool operator==(const AssetId& a, const AssetId& b) noexcept
    { return a.kind == b.kind && a.code == b.code && a.network == b.network; }
};
/// \brief One approved scale for an asset.
struct AssetScale {
    AssetId asset; ///< Asset identity.
    std::uint8_t scale{}; ///< Decimal places in minor_units.
};
/// \brief Explicit allowlist of exact asset scales.
class MoneyScalePolicy final {
public:
    std::vector<AssetScale> accepted_assets; ///< Approved asset-scale pairs.
    /// \brief Returns common fiat, BTC, ETH and stablecoin scales.
    /// \return Conservative standard scale policy.
    [[nodiscard]] static MoneyScalePolicy standard();
    /// \brief Finds the configured scale for an asset.
    /// \param asset Asset to look up.
    /// \return Scale or invalid-argument error.
    [[nodiscard]] Result<std::uint8_t> scale_for(const AssetId& asset) const;
};
/// \brief Exact amount in validated asset minor units.
struct Money {
    std::int64_t minor_units{}; ///< Non-negative exact amount.
    AssetId asset; ///< Currency or crypto asset.
    std::uint8_t scale{}; ///< Scale supplied by policy.
    /// \brief Creates money after exact policy scale validation.
    /// \param asset Asset identity.
    /// \param minor_units Non-negative amount.
    /// \param policy Authoritative scale policy.
    /// \return Valid money or invalid-argument error.
    [[nodiscard]] static Result<Money> from_minor_units(
        const AssetId& asset, std::int64_t minor_units,
        const MoneyScalePolicy& policy);
    /// \brief Creates standard fiat money.
    /// \param currency Three-letter fiat code.
    /// \param minor_units Non-negative amount.
    /// \return Valid fiat money or invalid-argument error.
    [[nodiscard]] static Result<Money> from_fiat_minor_units(
        std::string_view currency, std::int64_t minor_units);
    /// \brief Creates crypto money using an approved asset policy.
    /// \param asset Crypto symbol.
    /// \param network Network discriminator.
    /// \param minor_units Non-negative base-unit amount.
    /// \param policy Asset-scale policy.
    /// \return Valid crypto money or invalid-argument error.
    [[nodiscard]] static Result<Money> from_crypto_base_units(
        std::string_view asset, std::string_view network,
        std::int64_t minor_units, const MoneyScalePolicy& policy);
    /// \brief Compares exact money values.
    friend bool operator==(const Money& a, const Money& b) noexcept
    { return a.minor_units == b.minor_units && a.asset == b.asset
        && a.scale == b.scale; }
};

/// \brief Provider-neutral payment method.
enum class PaymentMethod : std::uint8_t {
    Unknown, Card, Mir, Sbp, Crypto, TelegramStars, Other,
};
/// \brief Common payment lifecycle state.
enum class PaymentStatus : std::uint8_t {
    Created, Pending, AwaitingConfirmation, Succeeded, Failed, Expired,
    Cancelled, UnknownOutcome, Refunded, PartiallyRefunded,
};
/// \brief Verified merchant account category.
enum class MerchantType : std::uint8_t {
    Unknown, Individual, SelfEmployed, IndividualEntrepreneur, Company,
};
/// \brief Product category for payment policy.
enum class ProductCategory : std::uint8_t {
    Unknown, PhysicalGoods, PhysicalServices, DigitalGoods, DigitalServices,
};
/// \brief Sales surface for payment policy.
enum class SalesSurface : std::uint8_t {
    Unknown, TelegramBot, TelegramMiniApp, ExternalWeb,
};
/// \brief Merchant-account onboarding evidence state.
enum class PaymentEligibilityState : std::uint8_t {
    Candidate, NeedsVerification, Eligible, Deferred, Unsupported, Rejected,
};

/// \brief Static provider capabilities, independent of merchant approval.
struct PaymentProviderCapabilities {
    ProviderId provider; ///< Provider owning this record.
    std::vector<PaymentMethod> methods; ///< Supported methods.
    std::vector<MerchantType> merchant_types; ///< Advertised merchant types.
    std::vector<AssetId> assets; ///< Supported assets.
    bool hosted_checkout = false; ///< Hosted checkout support.
    bool payment_links = false; ///< Payment-link support.
    bool direct_api = false; ///< Direct create API support.
    bool webhooks = false; ///< Authenticated webhook support.
    bool status_lookup = false; ///< Status-query support.
    bool refunds = false; ///< Refund support.
    bool partial_refunds = false; ///< Partial-refund support.
    bool inventory_hold = false; ///< Hold/capture support.
    bool sandbox = false; ///< Sandbox support.
    /// \brief Tests static method capability.
    /// \param method Method to test.
    /// \return True when supported.
    [[nodiscard]] bool supports_method(PaymentMethod method) const noexcept;
    /// \brief Tests static asset capability.
    /// \param asset Asset to test.
    /// \return True when supported.
    [[nodiscard]] bool supports_asset(const AssetId& asset) const noexcept;
};
/// \brief External merchant-account eligibility decision.
struct PaymentEligibilityDecision {
    ProviderId provider; ///< Provider covered by the decision.
    MerchantAccountId merchant_account; ///< Exact account covered.
    PaymentEligibilityState state = PaymentEligibilityState::NeedsVerification; ///< Decision state.
    MerchantType merchant_type = MerchantType::Unknown; ///< Verified account type.
    std::string jurisdiction; ///< Registration/residency code.
    std::vector<PaymentMethod> methods; ///< Account-approved methods.
    std::vector<AssetId> assets; ///< Account-approved assets.
    std::vector<ProductCategory> product_categories; ///< Approved product categories.
    std::optional<ClockTime> valid_until; ///< Evidence expiry.
    std::string evidence_source; ///< Immutable review/source identifier.
};
/// \brief Merchant and evidence context for fail-closed selection.
struct PaymentSelectionContext {
    MerchantAccountId merchant_account; ///< Settlement account.
    MerchantType merchant_type = MerchantType::Unknown; ///< Verified merchant type.
    std::string jurisdiction; ///< Registration/residency code.
    std::optional<ClockTime> now; ///< Time used for expiry checks.
    MoneyScalePolicy money_scale_policy = MoneyScalePolicy::standard(); ///< Authoritative amount scale policy.
    std::vector<PaymentEligibilityDecision> eligibility; ///< External decisions.
};

/// \brief Immutable request for one idempotent payment attempt.
struct PaymentCreateRequest {
    PaymentId payment_id; ///< Logical payment identity.
    PaymentAttemptId attempt_id; ///< Attempt identity.
    OrderId order_id; ///< Order identity.
    MerchantAccountId merchant_account; ///< Settlement account.
    Money amount; ///< Exact validated amount.
    PaymentMethod method = PaymentMethod::Unknown; ///< Requested method.
    ProductCategory product_category = ProductCategory::Unknown; ///< Sold category.
    SalesSurface sales_surface = SalesSurface::Unknown; ///< Checkout surface.
    std::string idempotency_key; ///< Retry key.
    std::string return_url; ///< Optional browser return URL.
};
/// \brief Provider-neutral create result.
struct PaymentCreateResult {
    ProviderPaymentId provider_payment; ///< Provider payment identity.
    PaymentStatus status = PaymentStatus::Pending; ///< Initial status.
    std::optional<std::string> checkout_url; ///< Hosted checkout URL.
};
/// \brief Authenticated evidence provenance.
struct PaymentEvidenceProvenance {
    ProviderId provider; ///< Authenticating provider.
    MerchantAccountId merchant_account; ///< Account in evidence.
    std::string event_id; ///< Provider event identity.
    std::string payload_digest; ///< Authenticated payload digest.
    std::string source; ///< Webhook or status-query source.
    std::string authentication_context; ///< Signed context summary.
};
/// \brief Authenticated provider status evidence.
struct PaymentStatusSnapshot {
    PaymentId payment_id; ///< Logical payment identity.
    PaymentAttemptId attempt_id; ///< Bound attempt identity.
    OrderId order_id; ///< Bound order identity.
    ProviderId provider; ///< Bound provider identity.
    MerchantAccountId merchant_account; ///< Bound merchant account.
    ProviderPaymentId provider_payment; ///< Provider payment identity.
    PaymentStatus status = PaymentStatus::UnknownOutcome; ///< Mapped status.
    Money amount; ///< Provider-reported amount.
    PaymentEvidenceProvenance provenance; ///< Authenticated provenance.
};
/// \brief Immutable full or partial refund request.
struct RefundRequest {
    PaymentId payment_id; ///< Logical payment identity.
    PaymentAttemptId attempt_id; ///< Attempt identity.
    MerchantAccountId merchant_account; ///< Refund account.
    Money amount; ///< Refund amount.
    std::string idempotency_key; ///< Refund retry key.
};
/// \brief Provider-neutral refund result.
struct RefundResult {
    RefundId refund_id; ///< Refund identity.
    PaymentStatus status = PaymentStatus::UnknownOutcome; ///< Refund status.
};
/// \brief Durable payment record shape.
struct PaymentRecord {
    PaymentId payment_id; ///< Logical payment identity.
    OrderId order_id; ///< Order identity.
    PaymentAttemptId attempt_id; ///< Current attempt.
    ProviderId provider; ///< Selected provider.
    MerchantAccountId merchant_account; ///< Settlement account.
    ProviderPaymentId provider_payment; ///< Provider payment identity.
    Money amount; ///< Expected amount.
    PaymentMethod method = PaymentMethod::Unknown; ///< Attempt method.
    ProductCategory product_category = ProductCategory::Unknown; ///< Product category.
    SalesSurface sales_surface = SalesSurface::Unknown; ///< Checkout surface.
    PaymentStatus status = PaymentStatus::Created; ///< Current status.
    std::string idempotency_key; ///< Immutable attempt key.
    std::uint64_t revision{}; ///< Optimistic revision.
};
/// \brief Per-shop provider and checkout policy.
struct PaymentPolicy {
    std::vector<ProviderId> allowed_providers; ///< Required provider allowlist.
    std::vector<ProviderId> provider_priority; ///< Priority inside allowlist.
    std::vector<PaymentMethod> allowed_methods; ///< Optional method restriction.
    std::vector<AssetId> allowed_assets; ///< Optional asset restriction.
    std::vector<ProductCategory> allowed_product_categories; ///< Optional category restriction.
    std::vector<SalesSurface> allowed_sales_surfaces; ///< Optional surface restriction.
    bool allow_external_checkout = false; ///< Explicit external-checkout gate.
    bool allow_telegram_stars = false; ///< Explicit Telegram digital gate.
    bool require_webhooks = true; ///< Require webhooks.
    bool require_status_lookup = true; ///< Require status lookup.
    bool require_refunds = true; ///< Require refunds.
    /// \brief Tests local product, surface and method policy.
    /// \param request Candidate request.
    /// \return True only when policy admits the request.
    [[nodiscard]] bool allows(const PaymentCreateRequest& request) const noexcept;
};
/// \brief Untrusted HTTP webhook input preserved before authentication.
struct RawWebhookRequest {
    std::string raw_payload; ///< Original bytes.
    std::string path; ///< Signed request path.
    std::vector<std::pair<std::string, std::string>> headers; ///< Original headers.
};
/// \brief Event deduplication record committed with payment state.
struct PaymentEventRecord {
    ProviderId provider; ///< Event provider.
    MerchantAccountId merchant_account; ///< Event account.
    std::string event_id; ///< Provider event identity.
    std::string payload_digest; ///< Immutable payload digest.
};

/// \brief Atomic payment state and event-deduplication transaction.
class PaymentTransaction {
public:
    virtual ~PaymentTransaction() = default;
    /// \brief Loads a payment in this transaction snapshot.
    /// \param payment_id Payment identity.
    /// \return Record, absence, or error.
    [[nodiscard]] virtual Result<std::optional<PaymentRecord>> load(
        const PaymentId& payment_id) const = 0;
    /// \brief Saves with optimistic revision fencing.
    /// \param record Candidate record.
    /// \param expected_revision Observed revision.
    /// \return Persisted record or conflict/error.
    [[nodiscard]] virtual Result<PaymentRecord> save(
        const PaymentRecord& record, std::uint64_t expected_revision) = 0;
    /// \brief Records an authenticated event for atomic deduplication.
    /// \param event Event identity and digest.
    /// \return Success or duplicate/conflict error.
    [[nodiscard]] virtual Result<void> record_event(
        const PaymentEventRecord& event) = 0;
    /// \brief Publishes staged state and events atomically.
    /// \return Commit success or unknown outcome.
    [[nodiscard]] virtual Result<void> commit() = 0;
    /// \brief Abandons all staged changes.
    virtual void rollback() noexcept = 0;
};
/// \brief Repository boundary for atomic payment transactions.
class PaymentRepository {
public:
    virtual ~PaymentRepository() = default;
    /// \brief Begins a payment/event transaction.
    /// \return Owned transaction or storage error.
    [[nodiscard]] virtual Result<std::unique_ptr<PaymentTransaction>>
    begin_transaction() = 0;
};

/// \brief Provider adapter boundary without SDK types.
class IPaymentProvider {
public:
    virtual ~IPaymentProvider() = default;
    /// \brief Returns static provider identity.
    /// \return Provider identity.
    [[nodiscard]] virtual ProviderId id() const noexcept = 0;
    /// \brief Returns static capabilities, not merchant approval.
    /// \return Capability metadata.
    [[nodiscard]] virtual PaymentProviderCapabilities capabilities() const = 0;
    /// \brief Creates one idempotent payment attempt.
    /// \param request Immutable payment request.
    /// \return Create result or observable error/unknown outcome.
    [[nodiscard]] virtual Result<PaymentCreateResult> create_payment(
        const PaymentCreateRequest& request) = 0;
    /// \brief Queries authoritative provider status.
    /// \param merchant_account Account scope.
    /// \param attempt_id Application attempt identity.
    /// \param provider_payment Provider payment identity.
    /// \return Authenticated status evidence or error.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> query_status(
        const MerchantAccountId& merchant_account,
        const PaymentAttemptId& attempt_id,
        const ProviderPaymentId& provider_payment) = 0;
    /// \brief Requests an idempotent refund.
    /// \param request Refund request.
    /// \return Refund result or observable error/unknown outcome.
    [[nodiscard]] virtual Result<RefundResult> refund(
        const RefundRequest& request) = 0;
};

/// \brief Deterministic registry for independently configured providers.
class PaymentProviderRegistry final {
public:
    /// \brief Registers a provider and rejects duplicate or inconsistent IDs.
    /// \param provider Shared provider adapter.
    /// \return Success or validation error.
    [[nodiscard]] Result<void> register_provider(
        std::shared_ptr<IPaymentProvider> provider);
    /// \brief Finds a provider by identity.
    /// \param provider Provider identity.
    /// \return Adapter or null.
    [[nodiscard]] std::shared_ptr<IPaymentProvider> find(
        const ProviderId& provider) const noexcept;
    /// \brief Selects only an allowlisted provider with matching account evidence.
    /// \param request Validated payment request.
    /// \param policy Allowlist, priority and checkout policy.
    /// \param context Merchant account and external onboarding evidence.
    /// \return Selected adapter or a fail-closed error.
    [[nodiscard]] Result<std::shared_ptr<IPaymentProvider>> select(
        const PaymentCreateRequest& request, const PaymentPolicy& policy,
        const PaymentSelectionContext& context) const;
private:
    std::vector<std::shared_ptr<IPaymentProvider>> m_providers;
};

/// \brief Payment creation and status orchestration boundary.
class PaymentService {
public:
    virtual ~PaymentService() = default;
    /// \brief Creates an attempt using deterministic provider selection.
    /// \param request Payment request.
    /// \param policy Shop policy.
    /// \param context Account and onboarding context.
    /// \return Create result or explicit error/unknown outcome.
    [[nodiscard]] virtual Result<PaymentCreateResult> create_payment(
        const PaymentCreateRequest& request, const PaymentPolicy& policy,
        const PaymentSelectionContext& context) = 0;
    /// \brief Queries status without treating redirect as proof.
    /// \param merchant_account Account scope.
    /// \param attempt_id Attempt identity.
    /// \param provider_payment Provider payment identity.
    /// \return Authenticated status evidence or error.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> query_status(
        const MerchantAccountId& merchant_account,
        const PaymentAttemptId& attempt_id,
        const ProviderPaymentId& provider_payment) = 0;
};
/// \brief Provider webhook authentication boundary.
class PaymentWebhookVerifier {
public:
    virtual ~PaymentWebhookVerifier() = default;
    /// \brief Verifies original bytes, headers, path and provider rules.
    /// \param request Untrusted request preserved without normalization.
    /// \return Authenticated evidence with complete provenance.
    [[nodiscard]] virtual Result<PaymentStatusSnapshot> verify_and_decode(
        const RawWebhookRequest& request) const = 0;
};
/// \brief Reconciliation boundary for authenticated evidence.
class PaymentReconciliation {
public:
    virtual ~PaymentReconciliation() = default;
    /// \brief Applies evidence with revision and event-idempotency rules.
    /// \param snapshot Authenticated provider evidence.
    /// \param transaction Atomic payment/event transaction.
    /// \param payment_id Payment identity receiving evidence.
    /// \return Updated record or conflict/error.
    [[nodiscard]] virtual Result<PaymentRecord> reconcile(
        const PaymentStatusSnapshot& snapshot, PaymentTransaction& transaction,
        const PaymentId& payment_id) = 0;
};
/// \brief Refund orchestration boundary.
class RefundService {
public:
    virtual ~RefundService() = default;
    /// \brief Requests a full or partial refund.
    /// \param request Refund request.
    /// \return Refund result or explicit error/unknown outcome.
    [[nodiscard]] virtual Result<RefundResult> request_refund(
        const RefundRequest& request) = 0;
};

/// \brief Validates a payment request before network I/O.
/// \param request Request to validate.
/// \return Success or invalid-argument error.
[[nodiscard]] Result<void> validate_payment_request(
    const PaymentCreateRequest& request);
/// \brief Validates a payment request against an explicit asset-scale policy.
/// \param request Request to validate.
/// \param policy Authoritative asset-scale policy.
/// \return Success or invalid-argument error.
[[nodiscard]] Result<void> validate_payment_request(
    const PaymentCreateRequest& request, const MoneyScalePolicy& policy);
/// \brief Validates authenticated evidence against the stored payment record.
/// \param snapshot Provider evidence to validate.
/// \param record Stored payment record to which it is applied.
/// \return Success or an identity, amount, provenance, or status error.
[[nodiscard]] Result<void> validate_payment_status_snapshot(
    const PaymentStatusSnapshot& snapshot, const PaymentRecord& record);
/// \brief Validates a refund request before network I/O.
/// \param request Request to validate.
/// \return Success or invalid-argument error.
[[nodiscard]] Result<void> validate_refund_request(
    const RefundRequest& request);

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENTS_HPP
