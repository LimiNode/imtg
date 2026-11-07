#include "imtg/commerce/payments.hpp"
#include "test_support.hpp"

#include <chrono>
#include <memory>

namespace {

using namespace imtg;
using namespace imtg::commerce;

class FakeProvider final : public IPaymentProvider {
public:
    FakeProvider(ProviderId provider, std::vector<PaymentMethod> methods,
                 std::vector<AssetId> assets)
        : m_capabilities{std::move(provider), std::move(methods),
                         {MerchantType::Individual}, std::move(assets),
                         true, true, true, true, true, true, true, true, true}
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
        const MerchantAccountId& merchant_account,
        const PaymentAttemptId& attempt_id,
        const ProviderPaymentId& provider_payment) override
    {
        PaymentStatusSnapshot snapshot;
        snapshot.attempt_id = attempt_id;
        snapshot.merchant_account = merchant_account;
        snapshot.provider_payment = provider_payment;
        snapshot.status = PaymentStatus::Succeeded;
        return snapshot;
    }

    Result<RefundResult> refund(const RefundRequest&) override
    {
        return RefundResult{RefundId{"refund-1"}, PaymentStatus::Refunded};
    }

    PaymentProviderCapabilities m_capabilities;
    PaymentAttemptId last_attempt;
};

Money rub()
{
    return Money::from_fiat_minor_units("RUB", 1000).value();
}

PaymentCreateRequest request()
{
    PaymentCreateRequest value;
    value.payment_id = PaymentId{"payment-1"};
    value.attempt_id = PaymentAttemptId{"attempt-1"};
    value.order_id = OrderId{"order-1"};
    value.merchant_account = MerchantAccountId{"merchant-1"};
    value.amount = rub();
    value.method = PaymentMethod::Mir;
    value.product_category = ProductCategory::PhysicalGoods;
    value.sales_surface = SalesSurface::ExternalWeb;
    value.idempotency_key = "idem-1";
    return value;
}

PaymentSelectionContext context(const PaymentCreateRequest& value,
                                const ProviderId& provider,
                                ClockTime valid_until)
{
    PaymentSelectionContext result;
    result.merchant_account = value.merchant_account;
    result.merchant_type = MerchantType::Individual;
    result.jurisdiction = "RU";
    result.now = ClockTime{std::chrono::seconds{100}};
    result.eligibility.push_back(PaymentEligibilityDecision{
        provider,
        value.merchant_account,
        PaymentEligibilityState::Eligible,
        MerchantType::Individual,
        "RU",
        {PaymentMethod::Mir},
        {value.amount.asset},
        {ProductCategory::PhysicalGoods},
        valid_until,
        "onboarding-review-1"});
    return result;
}

PaymentPolicy policy(const PaymentCreateRequest& value,
                     const ProviderId& provider)
{
    PaymentPolicy result;
    result.allowed_providers = {provider};
    result.provider_priority = {provider};
    result.allowed_methods = {value.method};
    result.allowed_assets = {value.amount.asset};
    result.allow_external_checkout = true;
    return result;
}

void test_money_and_validation()
{
    const auto money = Money::from_fiat_minor_units("RUB", 1000);
    CHECK(money);
    CHECK(money.value().scale == 2);
    const auto rub_asset = AssetId{AssetKind::Fiat, "RUB", std::string{}};
    CHECK(money.value().asset == rub_asset);
    CHECK(Money::from_fiat_minor_units("JPY", 100).value().scale == 0);
    CHECK(Money::from_fiat_minor_units("KWD", 100).value().scale == 3);
    CHECK(!Money::from_fiat_minor_units("ZZZ", 100));
    CHECK(!Money::from_fiat_minor_units("RUB", -1));
    CHECK(!Money::from_crypto_base_units(
        "USDT", "solana", 100, MoneyScalePolicy::standard()));

    PaymentCreateRequest value = request();
    CHECK(validate_payment_request(value));
    value.idempotency_key.clear();
    CHECK(!validate_payment_request(value));
    value = request();
    value.product_category = ProductCategory::Unknown;
    CHECK(!validate_payment_request(value));
    value = request();
    value.amount.scale = 0;
    CHECK(!validate_payment_request(value));
    MoneyScalePolicy custom{{{{AssetKind::Crypto, "USDT", "solana"}, 6}}};
    value.amount = Money::from_crypto_base_units("USDT", "solana", 100, custom).value();
    CHECK(validate_payment_request(value, custom));

    RefundRequest refund{value.payment_id, value.attempt_id,
                         value.merchant_account, rub(), "refund-idem-1"};
    CHECK(validate_refund_request(refund));
    refund.amount.minor_units = 0;
    CHECK(!validate_refund_request(refund));
}

void test_evidence_binding()
{
    const auto value = request();
    PaymentRecord record;
    record.payment_id = value.payment_id;
    record.order_id = value.order_id;
    record.attempt_id = value.attempt_id;
    record.provider = ProviderId{"eligible"};
    record.merchant_account = value.merchant_account;
    record.provider_payment = ProviderPaymentId{"provider-payment"};
    record.amount = value.amount;

    PaymentStatusSnapshot snapshot;
    snapshot.payment_id = record.payment_id;
    snapshot.attempt_id = record.attempt_id;
    snapshot.order_id = record.order_id;
    snapshot.provider = record.provider;
    snapshot.merchant_account = record.merchant_account;
    snapshot.provider_payment = ProviderPaymentId{"provider-payment"};
    snapshot.status = PaymentStatus::Succeeded;
    snapshot.amount = record.amount;
    snapshot.provenance = PaymentEvidenceProvenance{
        record.provider, record.merchant_account, "event-1", "digest-1",
        "webhook", "signed-context"};
    CHECK(validate_payment_status_snapshot(snapshot, record));
    snapshot.provenance.provider = ProviderId{"other"};
    CHECK(!validate_payment_status_snapshot(snapshot, record));
}

void test_registry_is_account_scoped_and_allowlisted()
{
    const auto value = request();
    PaymentProviderRegistry registry;
    const auto candidate_id = ProviderId{"candidate"};
    const auto eligible_id = ProviderId{"eligible"};
    auto candidate = std::make_shared<FakeProvider>(
        candidate_id, std::vector<PaymentMethod>{PaymentMethod::Mir},
        std::vector<AssetId>{value.amount.asset});
    auto eligible = std::make_shared<FakeProvider>(
        eligible_id, std::vector<PaymentMethod>{PaymentMethod::Mir},
        std::vector<AssetId>{value.amount.asset});
    CHECK(registry.register_provider(candidate));
    CHECK(registry.register_provider(eligible));
    CHECK(!registry.register_provider(eligible));

    auto selection_context = context(
        value, eligible_id, ClockTime{std::chrono::seconds{200}});
    auto selection_policy = policy(value, eligible_id);
    const auto selected = registry.select(value, selection_policy, selection_context);
    CHECK(selected && selected.value() == eligible);

    selection_policy.allowed_providers.clear();
    CHECK(!registry.select(value, selection_policy, selection_context));

    selection_policy = policy(value, candidate_id);
    CHECK(!registry.select(value, selection_policy, selection_context));

    selection_context.merchant_account = MerchantAccountId{"other-merchant"};
    CHECK(!registry.select(value, policy(value, eligible_id), selection_context));

    selection_context = context(
        value, eligible_id, ClockTime{std::chrono::seconds{99}});
    CHECK(!registry.select(value, policy(value, eligible_id), selection_context));
}

void test_telegram_policy_is_fail_closed()
{
    auto value = request();
    PaymentPolicy payment_policy;
    payment_policy.allow_external_checkout = true;
    value.product_category = ProductCategory::DigitalGoods;
    value.sales_surface = SalesSurface::TelegramBot;
    CHECK(!payment_policy.allows(value));

    value.method = PaymentMethod::TelegramStars;
    payment_policy.allow_telegram_stars = true;
    CHECK(payment_policy.allows(value));
    payment_policy.allow_telegram_stars = false;
    CHECK(!payment_policy.allows(value));
}

} // namespace

int main()
{
    test_money_and_validation();
    test_registry_is_account_scoped_and_allowlisted();
    test_telegram_policy_is_fail_closed();
    test_evidence_binding();
    return 0;
}
