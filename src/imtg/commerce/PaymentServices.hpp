#ifndef IMTG_COMMERCE_PAYMENT_SERVICES_HPP
#define IMTG_COMMERCE_PAYMENT_SERVICES_HPP

/// \file imtg/commerce/PaymentServices.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "PaymentProvider.hpp"
#include "PaymentTransaction.hpp"

namespace imtg::commerce {

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
/// \brief Validates authenticated evidence against a stored payment record.
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
/// \brief Validates a refund request against an explicit asset-scale policy.
/// \param request Refund request to validate.
/// \param policy Authoritative asset-scale policy.
/// \return Success or invalid-argument error.
[[nodiscard]] Result<void> validate_refund_request(
    const RefundRequest& request, const MoneyScalePolicy& policy);

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_SERVICES_HPP
