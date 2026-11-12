#ifndef IMTG_COMMERCE_PAYMENT_RECORDS_HPP
#define IMTG_COMMERCE_PAYMENT_RECORDS_HPP

/// \file imtg/commerce/PaymentRecords.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "PaymentTypes.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace imtg::commerce {

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
    ProductCategory product_category = ProductCategory::Unknown; ///< Category.
    SalesSurface sales_surface = SalesSurface::Unknown; ///< Checkout surface.
    PaymentStatus status = PaymentStatus::Created; ///< Current status.
    std::string idempotency_key; ///< Immutable attempt key.
    std::uint64_t revision{}; ///< Optimistic revision.
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

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_RECORDS_HPP
