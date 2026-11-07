#include "imtg/commerce/payments.hpp"

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

bool valid_currency(std::string_view value) noexcept
{
    if (value.size() != 3) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isupper(character) != 0;
    });
}

bool contains(const std::vector<PaymentMethod>& methods, PaymentMethod method) noexcept
{
    return std::find(methods.begin(), methods.end(), method) != methods.end();
}

bool contains(const std::vector<std::string>& currencies, std::string_view currency) noexcept
{
    return std::find(currencies.begin(), currencies.end(), currency) != currencies.end();
}

} // namespace

Result<Money> Money::from_minor_units(std::string_view currency,
                                      std::int64_t minor_units)
{
    if (!valid_currency(currency)) return invalid("currency must be three uppercase letters");
    if (minor_units <= 0) return invalid("money amount must be positive");
    return Money{minor_units, std::string(currency)};
}

bool PaymentProviderCapabilities::supports_method(PaymentMethod method) const noexcept
{
    return contains(methods, method);
}

bool PaymentPolicy::allows(const PaymentCreateRequest& request) const noexcept
{
    if (!allow_external_checkout && request.method != PaymentMethod::TelegramStars) {
        return false;
    }
    if (!allowed_methods.empty() && !contains(allowed_methods, request.method)) {
        return false;
    }
    if (!allowed_currencies.empty()
        && !contains(allowed_currencies, request.amount.currency)) {
        return false;
    }
    return true;
}

Result<void> validate_payment_request(const PaymentCreateRequest& request)
{
    if (!non_empty(request.attempt_id.value) || !non_empty(request.order_id.value)) {
        return invalid("payment request identity is empty or too large");
    }
    if (!valid_currency(request.amount.currency) || request.amount.minor_units <= 0) {
        return invalid("payment request amount is invalid");
    }
    if (!non_empty(request.idempotency_key)) {
        return invalid("payment idempotency key is empty or too large");
    }
    return {};
}

Result<void> validate_refund_request(const RefundRequest& request)
{
    if (!non_empty(request.payment_id.value) || !non_empty(request.attempt_id.value)) {
        return invalid("refund request identity is empty or too large");
    }
    if (!valid_currency(request.amount.currency) || request.amount.minor_units <= 0) {
        return invalid("refund amount is invalid");
    }
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
    if (find(provider_id)) return Error{ErrorCode::DuplicateId, "payment provider is already registered"};
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
    const auto found = std::find_if(m_providers.begin(), m_providers.end(),
                                    [&provider](const auto& candidate) {
                                        return candidate && candidate->id() == provider;
                                    });
    return found == m_providers.end() ? nullptr : *found;
}

Result<std::shared_ptr<IPaymentProvider>> PaymentProviderRegistry::select(
    const PaymentCreateRequest& request, const PaymentPolicy& policy) const
{
    const auto valid = validate_payment_request(request);
    if (!valid) return valid.error();
    if (!policy.allows(request)) return invalid("payment request is rejected by policy");

    const auto eligible = [&request, &policy](const auto& provider) {
        if (!provider) return false;
        const auto capabilities = provider->capabilities();
        if (capabilities.eligibility != PaymentEligibilityState::Eligible
            || !capabilities.supports_method(request.method)) {
            return false;
        }
        if (policy.require_webhooks && !capabilities.webhooks) return false;
        if (policy.require_status_lookup && !capabilities.status_lookup) return false;
        if (policy.require_refunds && !capabilities.refunds) return false;
        return true;
    };

    if (!policy.provider_priority.empty()) {
        for (const auto& provider_id : policy.provider_priority) {
            const auto provider = find(provider_id);
            if (eligible(provider)) return provider;
        }
    }

    for (const auto& provider : m_providers) {
        if (!provider) continue;
        const auto provider_id = provider->id();
        const auto prioritized = std::find(policy.provider_priority.begin(),
                                          policy.provider_priority.end(), provider_id)
            != policy.provider_priority.end();
        if (!prioritized && eligible(provider)) return provider;
    }
    return Error{ErrorCode::HandlerUnavailable,
                 "no eligible payment provider satisfies the policy"};
}

} // namespace imtg::commerce
