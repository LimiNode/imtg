#ifndef IMTG_COMMERCE_PAYMENT_TYPES_HPP
#define IMTG_COMMERCE_PAYMENT_TYPES_HPP

/// \file imtg/commerce/PaymentTypes.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include <imtg/core.hpp>

#include <cstdint>
#include <string>
#include <string_view>
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
    /// \brief Compares provider payment identities.
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
    std::string value; ///< Fulfillment identity.
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

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_TYPES_HPP
