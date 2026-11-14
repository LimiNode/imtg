#ifndef IMTG_COMMERCE_PAYMENT_TRANSACTION_HPP
#define IMTG_COMMERCE_PAYMENT_TRANSACTION_HPP

/// \file imtg/commerce/PaymentTransaction.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "PaymentRecords.hpp"

#include <imtg/storage.hpp>

#include <memory>
#include <optional>

namespace imtg::commerce {

/// \brief Atomic payment state and event-deduplication transaction.
class PaymentTransaction {
public:
    virtual ~PaymentTransaction() = default;
    /// \brief Returns the current transaction state.
    /// \return Active, committed, rolled back or unknown state.
    [[nodiscard]] virtual TransactionState state() const noexcept = 0;
    /// \brief Loads a payment in this transaction snapshot.
    /// \param payment_id Payment identity.
    /// \return Record, absence, or error.
    [[nodiscard]] virtual Result<std::optional<PaymentRecord>> load(
        const PaymentId& payment_id) const = 0;
    /// \brief Saves with optimistic revision fencing.
    /// \param record Candidate record.
    /// \param expected_revision Observed revision.
    /// \return Persisted record or conflict/error.
    [[nodiscard]] virtual Result<PaymentRecord> save(
        const PaymentRecord& record, std::uint64_t expected_revision) = 0;
    /// \brief Records an authenticated event for atomic deduplication.
    /// \param event Event identity and digest.
    /// \return Success for a new or identical duplicate, or conflict when the
    /// same event identity has a different digest.
    [[nodiscard]] virtual Result<void> record_event(
        const PaymentEventRecord& event) = 0;
    /// \brief Publishes staged state and events atomically.
    /// \return Shared storage commit outcome; `Unknown` is terminal and must
    /// be resolved by authoritative recovery, never blindly retried.
    [[nodiscard]] virtual CommitResult commit() = 0;
    /// \brief Abandons active staged changes.
    /// \note An `Unknown` transaction must not be rolled back or replayed.
    virtual void rollback() noexcept = 0;
};

/// \brief Repository boundary for atomic payment transactions.
class PaymentRepository {
public:
    virtual ~PaymentRepository() = default;
    /// \brief Begins a payment/event transaction.
    /// \return Owned transaction or storage error.
    [[nodiscard]] virtual Result<std::unique_ptr<PaymentTransaction>>
    begin_transaction() = 0;
};

} // namespace imtg::commerce

#endif // IMTG_COMMERCE_PAYMENT_TRANSACTION_HPP
