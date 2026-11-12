#ifndef IMTG_COMMERCE_PAYMENT_POLICY_HPP
#define IMTG_COMMERCE_PAYMENT_POLICY_HPP

/// \file imtg/commerce/PaymentPolicy.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "PaymentTypes.hpp"

#include <optional>
#include <string>
#include <vector>

namespace imtg::commerce {

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
    std::vector<ProductCategory> product_categories; ///< Approved categories.
    std::optional<ClockTime> valid_until; ///< Evidence expiry.
    std::string evidence_source; ///< Immutable review/source identifier.
};

/// \brief Merchant and evidence context for fail-closed selection.
struct PaymentSelectionContext {
    MerchantAccountId merchant_account; ///< Settlement account.
    MerchantType merchant_type = MerchantType::Unknown; ///< Verified type.
    std::string jurisdiction; ///< Registration/residency code.
    std::optional<ClockTime> now; ///< Time used for expiry checks.
    MoneyScalePolicy money_scale_policy = MoneyScalePolicy::standard(); ///< Amount policy.
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
    ProviderPaymentId provider_payment; ///< Provider-side payment identity.
    PaymentStatus status = PaymentStatus::Pending; ///< Initial status.
    std::optional<std::string> checkout_url; ///< Hosted checkout URL.
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

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_POLICY_HPP
