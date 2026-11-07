#include "imtg/commerce/payments.hpp"
#include "test_support.hpp"

#include <memory>

namespace {

using namespace imtg;
using namespace imtg::commerce;

class FakeProvider : public IPaymentProvider {
public:
    FakeProvider(ProviderId provider, PaymentEligibilityState eligibility,
                 std::vector<PaymentMethod> methods)
        : m_capabilities{std::move(provider), eligibility, std::move(methods),
                         {MerchantType::Individual}, true, true, true, true,
                         true, true, true, true, true}
    {
    }

    ProviderId id() const noexcept override { return m_capabilities.provider; }

    PaymentProviderCapabilities capabilities() const override { return m_capabilities; }

    Result<PaymentCreateResult> create_payment(
        const PaymentCreateRequest& request) override
    {
        last_attempt = request.attempt_id;
        return PaymentCreateResult{ProviderPaymentId{"provider-payment"},
                                   PaymentStatus::Pending,
                                   std::string{"https://checkout.invalid/payment"}};
    }

    Result<PaymentStatusSnapshot> query_status(
        const PaymentAttemptId&, const ProviderPaymentId& provider_payment) override
    {
        return PaymentStatusSnapshot{provider_payment, PaymentStatus::Succeeded,
                                     Money{1000, "RUB"}, "event-1", "digest"};
    }

    Result<RefundResult> refund(const RefundRequest&) override
    {
        return RefundResult{RefundId{"refund-1"}, PaymentStatus::Refunded};
    }

    PaymentProviderCapabilities m_capabilities;
    PaymentAttemptId last_attempt;
};

void test_money_and_validation()
{
    const auto money = Money::from_minor_units("RUB", 1000);
    CHECK(money);
    CHECK((money.value() == Money{1000, "RUB"}));
    CHECK(!Money::from_minor_units("rub", 1000));
    CHECK(!Money::from_minor_units("RUB", 0));

    PaymentCreateRequest request{PaymentAttemptId{"attempt-1"}, OrderId{"order-1"},
                                 money.value(), PaymentMethod::Mir, "idem-1", {}};
    CHECK(validate_payment_request(request));
    request.idempotency_key.clear();
    CHECK(!validate_payment_request(request));

    RefundRequest refund{PaymentId{"payment-1"}, PaymentAttemptId{"attempt-1"},
                         money.value(), "refund-idem-1"};
    CHECK(validate_refund_request(refund));
    refund.amount.minor_units = 0;
    CHECK(!validate_refund_request(refund));
}

void test_registry_selection()
{
    PaymentProviderRegistry registry;
    auto ineligible = std::make_shared<FakeProvider>(
        ProviderId{"candidate"}, PaymentEligibilityState::NeedsVerification,
        std::vector<PaymentMethod>{PaymentMethod::Mir});
    auto eligible = std::make_shared<FakeProvider>(
        ProviderId{"eligible"}, PaymentEligibilityState::Eligible,
        std::vector<PaymentMethod>{PaymentMethod::Mir, PaymentMethod::Sbp});
    CHECK(registry.register_provider(ineligible));
    CHECK(registry.register_provider(eligible));
    CHECK(!registry.register_provider(eligible));
    CHECK(registry.find(ProviderId{"eligible"}) == eligible);
    CHECK(!registry.find(ProviderId{"missing"}));

    const auto amount = Money::from_minor_units("RUB", 1000);
    CHECK(amount);
    PaymentCreateRequest request{PaymentAttemptId{"attempt-1"}, OrderId{"order-1"},
                                 amount.value(), PaymentMethod::Mir, "idem-1", {}};
    PaymentPolicy policy;
    policy.allowed_methods = {PaymentMethod::Mir};
    policy.allowed_currencies = {"RUB"};
    policy.provider_priority = {ProviderId{"candidate"}, ProviderId{"eligible"}};
    const auto selected = registry.select(request, policy);
    CHECK(selected && selected.value() == eligible);
    CHECK(eligible->create_payment(request));
    CHECK(eligible->last_attempt == request.attempt_id);

    policy.allow_external_checkout = false;
    CHECK(!registry.select(request, policy));
}

void test_registration_identity_and_capabilities()
{
    PaymentProviderRegistry registry;
    auto mismatched = std::make_shared<FakeProvider>(
        ProviderId{"provider-a"}, PaymentEligibilityState::Eligible,
        std::vector<PaymentMethod>{PaymentMethod::Card});
    CHECK(registry.register_provider(mismatched));
    CHECK(mismatched->capabilities().supports_method(PaymentMethod::Card));
    CHECK(!mismatched->capabilities().supports_method(PaymentMethod::Crypto));

    class WrongIdentityProvider final : public FakeProvider {
    public:
        WrongIdentityProvider()
            : FakeProvider(ProviderId{"capability-id"},
                           PaymentEligibilityState::Eligible,
                           {PaymentMethod::Card})
        {
            m_identity = ProviderId{"runtime-id"};
        }

        ProviderId id() const noexcept override { return m_identity; }

    private:
        ProviderId m_identity;
    };

    CHECK(!registry.register_provider(std::make_shared<WrongIdentityProvider>()));
}

} // namespace

int main()
{
    test_money_and_validation();
    test_registry_selection();
    test_registration_identity_and_capabilities();
    return 0;
}
