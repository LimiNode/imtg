#ifndef IMTG_COMMERCE_PAYMENT_PROVIDER_HPP
#define IMTG_COMMERCE_PAYMENT_PROVIDER_HPP

#include "PaymentPolicy.hpp"
#include "PaymentRecords.hpp"

#include <memory>
#include <vector>

namespace imtg::commerce {

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

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_PROVIDER_HPP
