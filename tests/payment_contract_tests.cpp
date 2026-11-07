#include "imtg/commerce/payments.hpp"
#include "test_support.hpp"

#include <chrono>
#include <memory>
#include <optional>

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

class FakePaymentTransaction final : public PaymentTransaction {
public:
    TransactionState state() const noexcept override { return m_state; }

    Result<std::optional<PaymentRecord>> load(const PaymentId&) const override
    {
        return std::optional<PaymentRecord>{};
    }

    Result<PaymentRecord> save(const PaymentRecord& record,
                               std::uint64_t) override
    {
        m_record = record;
        return record;
    }

    Result<void> record_event(const PaymentEventRecord& event) override
    {
        if (m_event.has_value()) {
            if (m_event->provider == event.provider
                && m_event->merchant_account == event.merchant_account
                && m_event->event_id == event.event_id
                && m_event->payload_digest == event.payload_digest) {
                return {};
            }
            return Error{ErrorCode::StorageConflict,
                         "payment event identity has a different digest"};
        }
        m_event = event;
        return {};
    }

    CommitResult commit() override
    {
        if (m_state != TransactionState::Active) {
            return CommitResult{CommitOutcome::Failed,
                                Error{ErrorCode::InvalidState,
                                      "payment transaction is not active"}};
        }
        m_committed = true;
        m_state = TransactionState::Committed;
        return CommitResult{CommitOutcome::Committed, std::nullopt};
    }

    void rollback() noexcept override
    {
        if (m_state == TransactionState::Active) {
            m_rolled_back = true;
            m_state = TransactionState::RolledBack;
            m_record.reset();
            m_event.reset();
        }
    }

    std::optional<PaymentRecord> m_record;
    std::optional<PaymentEventRecord> m_event;
    bool m_committed = false;
    bool m_rolled_back = false;
    TransactionState m_state = TransactionState::Active;
};

class FakePaymentRepository final : public PaymentRepository {
public:
    Result<std::unique_ptr<PaymentTransaction>> begin_transaction() override
    {
        auto transaction = std::make_unique<FakePaymentTransaction>();
        m_transaction = transaction.get();
        return std::unique_ptr<PaymentTransaction>(std::move(transaction));
    }

    FakePaymentTransaction* m_transaction = nullptr;
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
    value = request();
    value.amount.minor_units = 0;
    CHECK(!validate_payment_request(value));
    value = request();
    value.method = static_cast<PaymentMethod>(255);
    CHECK(!validate_payment_request(value));
    value = request();
    MoneyScalePolicy custom{{{{AssetKind::Crypto, "USDT", "solana"}, 6}}};
    value.amount = Money::from_crypto_base_units("USDT", "solana", 100, custom).value();
    CHECK(validate_payment_request(value, custom));

    RefundRequest refund{value.payment_id, value.attempt_id,
                         value.merchant_account, rub(), "refund-idem-1"};
    CHECK(validate_refund_request(refund));
    refund.amount.minor_units = 0;
    CHECK(!validate_refund_request(refund));
    refund.amount = value.amount;
    refund.amount.minor_units = 1;
    CHECK(validate_refund_request(refund, custom));
    MoneyScalePolicy conflicting{{
        {{AssetKind::Crypto, "USDT", "solana"}, 6},
        {{AssetKind::Crypto, "USDT", "solana"}, 8}}};
    CHECK(!conflicting.scale_for(value.amount.asset));
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
    snapshot.provenance.provider = record.provider;
    snapshot.provenance.merchant_account = MerchantAccountId{"other"};
    CHECK(!validate_payment_status_snapshot(snapshot, record));
    snapshot.provenance.merchant_account = record.merchant_account;
    snapshot.attempt_id = PaymentAttemptId{"other-attempt"};
    CHECK(!validate_payment_status_snapshot(snapshot, record));
    snapshot.attempt_id = record.attempt_id;
    snapshot.provenance.authentication_context.clear();
    CHECK(!validate_payment_status_snapshot(snapshot, record));
}

void test_selection_rejects_context_and_capability_mismatches()
{
    const auto value = request();
    const auto provider_id = ProviderId{"provider"};
    auto provider = std::make_shared<FakeProvider>(
        provider_id, std::vector<PaymentMethod>{PaymentMethod::Mir},
        std::vector<AssetId>{value.amount.asset});
    PaymentProviderRegistry registry;
    CHECK(registry.register_provider(provider));

    auto selection_context = context(
        value, provider_id, ClockTime{std::chrono::seconds{200}});
    auto selection_policy = policy(value, provider_id);
    selection_context.merchant_type = MerchantType::Company;
    CHECK(!registry.select(value, selection_policy, selection_context));

    selection_context = context(
        value, provider_id, ClockTime{std::chrono::seconds{200}});
    selection_context.jurisdiction = "US";
    CHECK(!registry.select(value, selection_policy, selection_context));

    auto usd_request = value;
    usd_request.amount = Money::from_fiat_minor_units("USD", 1000).value();
    auto usd_policy = policy(usd_request, provider_id);
    auto usd_context = context(
        usd_request, provider_id, ClockTime{std::chrono::seconds{200}});
    CHECK(!registry.select(usd_request, usd_policy, usd_context));
}

void test_payment_transaction_is_the_deduplication_authority()
{
    FakePaymentRepository repository;
    auto transaction_result = repository.begin_transaction();
    CHECK(transaction_result);
    auto transaction = std::move(transaction_result).value();
    CHECK(transaction->record_event(PaymentEventRecord{
        ProviderId{"provider"}, MerchantAccountId{"merchant-1"},
        "event-1", "digest-1"}));
    CHECK(transaction->record_event(PaymentEventRecord{
        ProviderId{"provider"}, MerchantAccountId{"merchant-1"},
        "event-1", "digest-1"}));
    CHECK(!transaction->record_event(PaymentEventRecord{
        ProviderId{"provider"}, MerchantAccountId{"merchant-1"},
        "event-1", "digest-2"}));
    CHECK(transaction->commit().outcome == CommitOutcome::Committed);
    auto* fake = repository.m_transaction;
    CHECK(fake != nullptr);
    CHECK(fake->m_event.has_value());
    CHECK(fake->m_committed);
    CHECK(fake->state() == TransactionState::Committed);

    auto rollback_result = repository.begin_transaction();
    CHECK(rollback_result);
    auto rollback_transaction = std::move(rollback_result).value();
    CHECK(rollback_transaction->record_event(PaymentEventRecord{
        ProviderId{"provider"}, MerchantAccountId{"merchant-1"},
        "event-2", "digest-2"}));
    rollback_transaction->rollback();
    CHECK(rollback_transaction->state() == TransactionState::RolledBack);
    CHECK(rollback_transaction->commit().outcome == CommitOutcome::Failed);
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

    auto contradictory_context = context(
        value, eligible_id, ClockTime{std::chrono::seconds{200}});
    auto contradictory = contradictory_context.eligibility.front();
    contradictory.state = PaymentEligibilityState::Rejected;
    contradictory_context.eligibility.push_back(contradictory);
    CHECK(!registry.select(value, selection_policy, contradictory_context));
    std::swap(contradictory_context.eligibility.front(),
              contradictory_context.eligibility.back());
    CHECK(!registry.select(value, selection_policy, contradictory_context));

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

    payment_policy.allow_external_checkout = false;
    value = request();
    value.sales_surface = SalesSurface::TelegramBot;
    CHECK(!payment_policy.allows(value));
    value.sales_surface = SalesSurface::TelegramMiniApp;
    CHECK(!payment_policy.allows(value));
    value.sales_surface = SalesSurface::ExternalWeb;
    CHECK(!payment_policy.allows(value));
    payment_policy.allow_external_checkout = true;
    CHECK(payment_policy.allows(value));

    value = request();
    value.product_category = ProductCategory::DigitalServices;
    value.sales_surface = SalesSurface::TelegramMiniApp;
    CHECK(!payment_policy.allows(value));
}

} // namespace

int main()
{
    test_money_and_validation();
    test_registry_is_account_scoped_and_allowlisted();
    test_telegram_policy_is_fail_closed();
    test_evidence_binding();
    test_selection_rejects_context_and_capability_mismatches();
    test_payment_transaction_is_the_deduplication_authority();
    return 0;
}
