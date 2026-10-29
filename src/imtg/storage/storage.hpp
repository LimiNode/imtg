#ifndef IMTG_STORAGE_STORAGE_HPP
#define IMTG_STORAGE_STORAGE_HPP

#include "imtg/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace imtg {

using StorageBytes = std::string;
/// Zero means that a logical key has no generation; visible records always use non-zero values.
using StorageRevision = std::uint64_t;

/// \brief Returns the next non-zero record generation or a typed exhaustion error.
/// \thread_safety Safe to call concurrently.
[[nodiscard]] Result<StorageRevision> next_storage_revision(StorageRevision current);

enum class TransactionMode {
    ReadOnly,
    ReadWrite,
};

enum class TransactionState {
    Active,
    Committed,
    RolledBack,
    Unknown,
};

enum class MigrationState {
    Stable,
    Migrating,
    RecoveryRequired,
};

enum class CommitOutcome {
    Committed,
    RejectedConflict,
    Failed,
    Unknown,
};

struct CommitResult {
    CommitOutcome outcome;
    std::optional<Error> error;
};

struct StorageRecord {
    StorageBytes value;
    StorageRevision revision{};
};

struct StorageSchema {
    std::uint32_t profile_version{};
    std::unordered_map<std::string, std::uint32_t> namespace_versions;
    MigrationState migration_state = MigrationState::Stable;

    friend bool operator==(const StorageSchema& left, const StorageSchema& right)
    {
        return left.profile_version == right.profile_version
            && left.namespace_versions == right.namespace_versions
            && left.migration_state == right.migration_state;
    }
};

class ITransaction {
public:
    virtual ~ITransaction() = default;

    [[nodiscard]] virtual TransactionMode mode() const noexcept = 0;
    [[nodiscard]] virtual TransactionState state() const noexcept = 0;
    /// Reads from the transaction snapshot, including this transaction's pending writes.
    [[nodiscard]] virtual Result<std::optional<StorageRecord>> read(
        std::string_view name_space, std::string_view key) = 0;
    /// Registers a compare-only dependency checked atomically at commit. The
    /// dependency does not create a write and is useful for CAS-style reads.
    [[nodiscard]] virtual Result<void> watch(
        std::string_view, std::string_view,
        StorageRevision) {
        return Error{ErrorCode::StorageUnavailable,
                     "compare-only read dependencies are not supported by this backend"};
    }
    /// Creates a new key generation and returns its candidate committed revision.
    [[nodiscard]] virtual Result<StorageRevision> create_if_absent(
        std::string_view name_space, std::string_view key, std::string_view value) = 0;
    /// Updates a visible record using its transaction-visible revision.
    /// Repeated writes in one transaction return the same candidate committed revision.
    [[nodiscard]] virtual Result<StorageRevision> write(
        std::string_view name_space,
        std::string_view key,
        std::string_view value,
        StorageRevision expected_revision) = 0;
    [[nodiscard]] virtual Result<void> erase(
        std::string_view name_space,
        std::string_view key,
        StorageRevision expected_revision) = 0;
    /// Publishes the write set or returns a terminal commit outcome.
    [[nodiscard]] virtual CommitResult commit() = 0;
    /// Rolls back only an active transaction owned by the calling thread.
    virtual void rollback() noexcept = 0;
};

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    /// The returned transaction borrows the backend; the backend must outlive it.
    /// Implementations fail closed when schema metadata does not permit transaction intake.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) = 0;
    [[nodiscard]] virtual Result<StorageSchema> schema() const = 0;
    [[nodiscard]] virtual Result<void> validate_schema(const StorageSchema& expected) const = 0;
};

/// \brief Move-only RAII owner for one backend transaction.
class UnitOfWork {
public:
    UnitOfWork() noexcept = default;
    explicit UnitOfWork(std::unique_ptr<ITransaction> transaction) noexcept
        : m_transaction(std::move(transaction))
    {
    }
    UnitOfWork(const UnitOfWork&) = delete;
    UnitOfWork& operator=(const UnitOfWork&) = delete;
    UnitOfWork(UnitOfWork&&) noexcept = default;
    /// Replaces ownership after rolling back the currently owned active transaction.
    UnitOfWork& operator=(UnitOfWork&& other) noexcept;
    ~UnitOfWork();

    [[nodiscard]] static Result<UnitOfWork> begin(IStorageBackend& backend,
                                                  TransactionMode mode);
    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(m_transaction); }
    [[nodiscard]] ITransaction* transaction() noexcept { return m_transaction.get(); }
    [[nodiscard]] const ITransaction* transaction() const noexcept
    {
        return m_transaction.get();
    }
    /// Commits the owned transaction; `Unknown` remains terminal and is not retried.
    [[nodiscard]] CommitResult commit();
    /// Rolls back an active transaction. Unknown outcomes are never rolled back.
    void rollback() noexcept;

private:
    std::unique_ptr<ITransaction> m_transaction;
};

struct InMemoryFailureInjection {
    bool fail_write_set_preparation = false;
    bool fail_before_publish = false;
    bool unknown_before_publish = false;
    bool unknown_after_publish = false;
};

/// \brief Deterministic snapshot/optimistic backend used as the M2 semantic baseline.
/// \details Supports multiple active optimistic writers. Deleted keys retain a generation
/// tombstone for the backend lifetime so stale pre-delete transactions cannot overwrite a
/// recreated record. This backend is not durable.
class InMemoryStorageBackend final : public IStorageBackend {
public:
    explicit InMemoryStorageBackend(StorageSchema schema = StorageSchema{1, {}});
    ~InMemoryStorageBackend() override;

    [[nodiscard]] Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) override;
    [[nodiscard]] Result<StorageSchema> schema() const override;
    [[nodiscard]] Result<void> validate_schema(const StorageSchema& expected) const override;

    /// \brief Installs one-shot failures for deterministic tests.
    void set_failure_injection(InMemoryFailureInjection injection);
    [[nodiscard]] std::size_t record_count() const noexcept;

private:
    class Transaction;
    friend class Transaction;

    struct StoredRecord {
        std::optional<StorageBytes> value;
        StorageRevision revision{};
    };
    struct RecordKey {
        std::string name_space;
        std::string key;

        friend bool operator==(const RecordKey& left, const RecordKey& right) noexcept
        {
            return left.name_space == right.name_space && left.key == right.key;
        }
    };
    struct RecordKeyHash {
        std::size_t operator()(const RecordKey& value) const noexcept;
    };

    mutable std::mutex m_mutex;
    StorageSchema m_schema;
    std::unordered_map<RecordKey, StoredRecord, RecordKeyHash> m_records;
    InMemoryFailureInjection m_failure_injection;
};

} // namespace imtg

#endif // IMTG_STORAGE_STORAGE_HPP
