#include <imtg/commerce.hpp>

#include <algorithm>
#include <cctype>

namespace imtg::commerce {
namespace {

Error invalid(const char* message)
{
    return Error{ErrorCode::InvalidArgument, message};
}

bool non_empty(std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 256;
}

bool valid_code(std::string_view value, std::size_t maximum) noexcept
{
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-';
    });
}

bool valid_fiat_code(std::string_view value) noexcept
{
    if (value.size() != 3) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isupper(character) != 0;
    });
}

bool valid_jurisdiction(std::string_view value) noexcept
{
    if (value.size() != 2) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isupper(character) != 0;
    });
}

bool valid_asset(const AssetId& asset) noexcept
{
    if (asset.kind == AssetKind::Fiat) {
        return valid_fiat_code(asset.code) && asset.network.empty();
    }
    if (asset.kind == AssetKind::Crypto) {
        return valid_code(asset.code, 32) && valid_code(asset.network, 64);
    }
    return false;
}

bool valid_method(PaymentMethod method) noexcept
{
    return method >= PaymentMethod::Card && method <= PaymentMethod::Other;
}

bool valid_product(ProductCategory category) noexcept
{
    return category >= ProductCategory::PhysicalGoods
        && category <= ProductCategory::DigitalServices;
}

bool valid_surface(SalesSurface surface) noexcept
{
    return surface >= SalesSurface::TelegramBot
        && surface <= SalesSurface::ExternalWeb;
}

bool valid_status(PaymentStatus status) noexcept
{
    return status <= PaymentStatus::PartiallyRefunded;
}

template <typename T>
bool contains(const std::vector<T>& values, const T& value) noexcept
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool is_telegram_surface(SalesSurface surface) noexcept
{
    return surface == SalesSurface::TelegramBot
        || surface == SalesSurface::TelegramMiniApp;
}

const PaymentEligibilityDecision* find_decision(
    const PaymentSelectionContext& context, const ProviderId& provider)
{
    const PaymentEligibilityDecision* result = nullptr;
    for (const auto& decision : context.eligibility) {
        if (!(decision.provider == provider)
            || !(decision.merchant_account == context.merchant_account)) {
            continue;
        }
        if (result != nullptr) return nullptr;
        result = &decision;
    }
    return result;
}

bool allowlisted(const PaymentPolicy& policy, const ProviderId& provider) noexcept
{
    return contains(policy.allowed_providers, provider);
}

} // namespace

MoneyScalePolicy MoneyScalePolicy::standard()
{
    return MoneyScalePolicy{{
        {{AssetKind::Fiat, "RUB", {}}, 2},
        {{AssetKind::Fiat, "USD", {}}, 2},
        {{AssetKind::Fiat, "EUR", {}}, 2},
        {{AssetKind::Fiat, "GBP", {}}, 2},
        {{AssetKind::Fiat, "CNY", {}}, 2},
        {{AssetKind::Fiat, "JPY", {}}, 0},
        {{AssetKind::Fiat, "KWD", {}}, 3},
        {{AssetKind::Crypto, "BTC", "bitcoin"}, 8},
        {{AssetKind::Crypto, "ETH", "ethereum"}, 18},
        {{AssetKind::Crypto, "USDT", "ethereum"}, 6},
        {{AssetKind::Crypto, "USDT", "tron"}, 6},
        {{AssetKind::Crypto, "USDC", "ethereum"}, 6},
    }};
}

Result<std::uint8_t> MoneyScalePolicy::scale_for(const AssetId& asset) const
{
    if (!valid_asset(asset)) return invalid("asset identity is invalid");
    for (std::size_t index = 0; index < accepted_assets.size(); ++index) {
        const auto& entry = accepted_assets[index];
        if (!valid_asset(entry.asset) || entry.scale > 18) {
            return invalid("asset-scale policy contains an invalid entry");
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (accepted_assets[prior].asset == entry.asset) {
                return invalid("asset-scale policy contains duplicate asset definitions");
            }
        }
    }
    const auto found = std::find_if(
        accepted_assets.begin(), accepted_assets.end(),
        [&asset](const auto& entry) { return entry.asset == asset; });
    if (found == accepted_assets.end() || found->scale > 18) {
        return invalid("asset scale is not approved by policy");
    }
    return found->scale;
}

Result<Money> Money::from_minor_units(
    const AssetId& asset, std::int64_t minor_units,
    const MoneyScalePolicy& policy)
{
    if (minor_units < 0) return invalid("money amount must not be negative");
    const auto scale = policy.scale_for(asset);
    if (!scale) return scale.error();
    return Money{minor_units, asset, scale.value()};
}

Result<Money> Money::from_fiat_minor_units(
    std::string_view currency, std::int64_t minor_units)
{
    if (!valid_fiat_code(currency)) return invalid("fiat currency code is invalid");
    const auto asset = AssetId{AssetKind::Fiat, std::string(currency), {}};
    return from_minor_units(asset, minor_units, MoneyScalePolicy::standard());
}

Result<Money> Money::from_crypto_base_units(
    std::string_view asset, std::string_view network,
    std::int64_t minor_units, const MoneyScalePolicy& policy)
{
    const auto identity = AssetId{AssetKind::Crypto, std::string(asset),
                                  std::string(network)};
    return from_minor_units(identity, minor_units, policy);
}

Result<void> validate_money(const Money& amount, const MoneyScalePolicy& policy)
{
    if (amount.minor_units < 0 || !valid_asset(amount.asset)) {
        return invalid("money amount or asset is invalid");
    }
    const auto scale = policy.scale_for(amount.asset);
    if (!scale || scale.value() != amount.scale) {
        return invalid("money scale is not approved by policy");
    }
    return {};
}

bool PaymentProviderCapabilities::supports_method(PaymentMethod method) const noexcept
{
    return contains(methods, method);
}

bool PaymentProviderCapabilities::supports_asset(const AssetId& asset) const noexcept
{
    return contains(assets, asset);
}

bool PaymentPolicy::allows(const PaymentCreateRequest& request) const noexcept
{
    if (!valid_method(request.method) || !valid_product(request.product_category)
        || !valid_surface(request.sales_surface)) {
        return false;
    }
    if (!allowed_methods.empty() && !contains(allowed_methods, request.method)) {
        return false;
    }
    if (!allowed_assets.empty() && !contains(allowed_assets, request.amount.asset)) {
        return false;
    }
    if (!allowed_product_categories.empty()
        && !contains(allowed_product_categories, request.product_category)) {
        return false;
    }
    if (!allowed_sales_surfaces.empty()
        && !contains(allowed_sales_surfaces, request.sales_surface)) {
        return false;
    }
    const bool digital = request.product_category == ProductCategory::DigitalGoods
        || request.product_category == ProductCategory::DigitalServices;
    if (request.method == PaymentMethod::TelegramStars) {
        return allow_telegram_stars && digital && is_telegram_surface(request.sales_surface);
    }
    if (digital && is_telegram_surface(request.sales_surface)) return false;
    if (request.method != PaymentMethod::TelegramStars && !allow_external_checkout) {
        return false;
    }
    return true;
}

Result<void> validate_payment_request(const PaymentCreateRequest& request)
{
    return validate_payment_request(request, MoneyScalePolicy::standard());
}

Result<void> validate_payment_request(
    const PaymentCreateRequest& request, const MoneyScalePolicy& policy)
{
    if (!non_empty(request.payment_id.value)
        || !non_empty(request.attempt_id.value)
        || !non_empty(request.order_id.value)
        || !non_empty(request.merchant_account.value)) {
        return invalid("payment identity is empty or too large");
    }
    const auto amount = validate_money(request.amount, policy);
    if (!amount) return amount.error();
    if (request.amount.minor_units == 0) {
        return invalid("payment amount must be positive");
    }
    if (!valid_method(request.method) || !valid_product(request.product_category)
        || !valid_surface(request.sales_surface)) {
        return invalid("payment method, product or surface is invalid");
    }
    if (!non_empty(request.idempotency_key)) {
        return invalid("payment idempotency key is empty or too large");
    }
    return {};
}

Result<void> validate_payment_status_snapshot(
    const PaymentStatusSnapshot& snapshot, const PaymentRecord& record)
{
    if (!non_empty(snapshot.payment_id.value)
        || !non_empty(snapshot.attempt_id.value)
        || !non_empty(snapshot.order_id.value)
        || !non_empty(snapshot.provider.value)
        || !non_empty(snapshot.merchant_account.value)
        || !non_empty(snapshot.provider_payment.value)
        || !valid_status(snapshot.status)
        || !(snapshot.payment_id == record.payment_id)
        || !(snapshot.attempt_id == record.attempt_id)
        || !(snapshot.order_id == record.order_id)
        || !(snapshot.provider == record.provider)
        || !(snapshot.merchant_account == record.merchant_account)
        || !(snapshot.provider_payment == record.provider_payment)
        || !(snapshot.amount == record.amount)
        || !(snapshot.provenance.provider == snapshot.provider)
        || !(snapshot.provenance.merchant_account == snapshot.merchant_account)
        || !non_empty(snapshot.provenance.event_id)
        || !non_empty(snapshot.provenance.payload_digest)
        || !non_empty(snapshot.provenance.source)
        || !non_empty(snapshot.provenance.authentication_context)) {
        return invalid("payment status evidence is not bound to the stored payment");
    }
    return {};
}

Result<void> validate_refund_request(const RefundRequest& request)
{
    return validate_refund_request(request, MoneyScalePolicy::standard());
}

Result<void> validate_refund_request(
    const RefundRequest& request, const MoneyScalePolicy& policy)
{
    if (!non_empty(request.payment_id.value)
        || !non_empty(request.attempt_id.value)
        || !non_empty(request.merchant_account.value)) {
        return invalid("refund identity is empty or too large");
    }
    if (request.amount.minor_units <= 0) {
        return invalid("refund amount must be positive");
    }
    const auto amount = validate_money(request.amount, policy);
    if (!amount) return amount.error();
    if (!non_empty(request.idempotency_key)) {
        return invalid("refund idempotency key is empty or too large");
    }
    return {};
}

Result<void> PaymentProviderRegistry::register_provider(
    std::shared_ptr<IPaymentProvider> provider)
{
    if (!provider) return invalid("payment provider is null");
    const auto provider_id = provider->id();
    if (!non_empty(provider_id.value)) return invalid("payment provider ID is invalid");
    if (find(provider_id)) {
        return Error{ErrorCode::DuplicateId, "payment provider is already registered"};
    }
    const auto capabilities = provider->capabilities();
    if (!(capabilities.provider == provider_id)) {
        return invalid("provider capability identity does not match provider ID");
    }
    m_providers.push_back(std::move(provider));
    return {};
}

std::shared_ptr<IPaymentProvider> PaymentProviderRegistry::find(
    const ProviderId& provider) const noexcept
{
    const auto found = std::find_if(
        m_providers.begin(), m_providers.end(),
        [&provider](const auto& candidate) {
            return candidate && candidate->id() == provider;
        });
    return found == m_providers.end() ? nullptr : *found;
}

Result<std::shared_ptr<IPaymentProvider>> PaymentProviderRegistry::select(
    const PaymentCreateRequest& request, const PaymentPolicy& policy,
    const PaymentSelectionContext& context) const
{
    const auto valid = validate_payment_request(request, context.money_scale_policy);
    if (!valid) return valid.error();
    if (!policy.allows(request)) return invalid("payment request is rejected by policy");
    if (policy.allowed_providers.empty()) {
        return invalid("payment provider allowlist is required");
    }
    if (!(request.merchant_account == context.merchant_account)
        || !non_empty(context.merchant_account.value)
        || !valid_jurisdiction(context.jurisdiction)
        || context.merchant_type == MerchantType::Unknown
        || !context.now.has_value()) {
        return invalid("payment selection context is incomplete");
    }

    const auto eligible = [&request, &policy, &context](const auto& provider) {
        if (!provider) return false;
        const auto provider_id = provider->id();
        if (!allowlisted(policy, provider_id)) return false;
        const auto capabilities = provider->capabilities();
        if (!(capabilities.provider == provider_id)
            || !capabilities.supports_method(request.method)
            || !capabilities.supports_asset(request.amount.asset)
            || !contains(capabilities.merchant_types, context.merchant_type)) {
            return false;
        }
        if (policy.require_webhooks && !capabilities.webhooks) return false;
        if (policy.require_status_lookup && !capabilities.status_lookup) return false;
        if (policy.require_refunds && !capabilities.refunds) return false;
        const auto* decision = find_decision(context, provider_id);
        if (!decision || decision->state != PaymentEligibilityState::Eligible
            || decision->merchant_type != context.merchant_type
            || decision->jurisdiction != context.jurisdiction
            || !decision->valid_until.has_value()
            || *decision->valid_until <= *context.now
            || decision->evidence_source.empty()
            || !contains(decision->methods, request.method)
            || !contains(decision->assets, request.amount.asset)
            || !contains(decision->product_categories, request.product_category)) {
            return false;
        }
        return true;
    };

    for (const auto& provider_id : policy.provider_priority) {
        if (!allowlisted(policy, provider_id)) continue;
        const auto provider = find(provider_id);
        if (eligible(provider)) return provider;
    }
    for (const auto& provider : m_providers) {
        if (eligible(provider)) return provider;
    }
    return Error{ErrorCode::HandlerUnavailable,
                 "no allowlisted eligible payment provider satisfies the policy"};
}

} // namespace imtg::commerce
