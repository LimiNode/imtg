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
#include <vector>

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

/// \brief Terminal outcome of one transaction commit attempt.
struct CommitResult {
    CommitOutcome outcome; ///< Publication status; `Unknown` must never be retried blindly.
    std::optional<Error> error; ///< Diagnostic for non-committed outcomes.
};

/// \brief Value and non-zero generation visible in a transaction snapshot.
struct StorageRecord {
    StorageBytes value; ///< Backend-neutral record bytes.
    StorageRevision revision{}; ///< Optimistic-concurrency generation.
};

/// \brief One visible record returned by a bounded namespace scan.
struct StorageScanEntry {
    std::string key; ///< Canonical key of the visible record.
    StorageRecord record; ///< Value and generation observed in the snapshot.
};

/// \brief Version metadata governing backend admission and migrations.
struct StorageSchema {
    std::uint32_t profile_version{}; ///< Overall storage profile version.
    std::unordered_map<std::string, std::uint32_t> namespace_versions; ///< Per-namespace schemas.
    MigrationState migration_state = MigrationState::Stable; ///< Admission/recovery state.

    /// \brief Compares all schema metadata fields.
    friend bool operator==(const StorageSchema& left, const StorageSchema& right)
    {
        return left.profile_version == right.profile_version
            && left.namespace_versions == right.namespace_versions
            && left.migration_state == right.migration_state;
    }
};

/// \brief Thread-affine snapshot transaction with optimistic commit fencing.
/// \note The originating backend must outlive the transaction. Unless an implementation
/// documents otherwise, all operations are called from the creating thread.
class ITransaction {
public:
    virtual ~ITransaction() = default;

    /// \brief Returns the read-only or read-write transaction mode.
    /// \return Mode selected at transaction creation.
    [[nodiscard]] virtual TransactionMode mode() const noexcept = 0;
    /// \brief Returns the current terminal or active state.
    /// \return Current transaction state.
    [[nodiscard]] virtual TransactionState state() const noexcept = 0;
    /// \brief Reads from the transaction snapshot, including pending local writes.
    /// \param name_space Namespace containing the record.
    /// \param key Canonical record key.
    /// \return The visible record, or an empty optional when absent.
    [[nodiscard]] virtual Result<std::optional<StorageRecord>> read(
        std::string_view name_space, std::string_view key) = 0;
    /// \brief Returns a bounded lexicographic page of visible records.
    /// \param name_space Namespace to enumerate.
    /// \param continuation Exclusive key cursor; empty starts at the first key.
    /// \param limit Maximum number of records to return; must be non-zero.
    /// \return At most `limit` entries and no hidden/tombstoned records.
    /// \note The page is snapshot-consistent. It is a scan primitive, not a
    /// predicate lock; callers must watch records whose revisions they claim.
    [[nodiscard]] virtual Result<std::vector<StorageScanEntry>> scan(
        std::string_view name_space, std::string_view continuation,
        std::size_t limit)
    {
        (void)name_space;
        (void)continuation;
        (void)limit;
        return Error{ErrorCode::StorageUnavailable,
                     "bounded scans are not supported by this backend"};
    }
    /// \brief Registers a compare-only dependency checked atomically at commit.
    /// \param name_space Namespace containing the record.
    /// \param key Canonical record key.
    /// \param expected_revision Transaction-visible revision to fence.
    /// \return Success when the visible revision matches, or a typed conflict.
    /// \note A pending local write is fenced against its original backend
    /// generation; this method never creates a write itself.
    [[nodiscard]] virtual Result<void> watch(
        std::string_view name_space, std::string_view key,
        StorageRevision expected_revision) {
        (void)name_space;
        (void)key;
        (void)expected_revision;
        return Error{ErrorCode::StorageUnavailable,
                     "compare-only read dependencies are not supported by this backend"};
    }
    /// \brief Creates a new key generation in the local write set.
    /// \param name_space Namespace containing the record.
    /// \param key Canonical record key, absent from the transaction snapshot.
    /// \param value Bytes to stage.
    /// \return Candidate committed revision or a typed conflict/error.
    [[nodiscard]] virtual Result<StorageRevision> create_if_absent(
        std::string_view name_space, std::string_view key, std::string_view value) = 0;
    /// \brief Updates a visible record using its transaction-visible revision.
    /// \param name_space Namespace containing the record.
    /// \param key Canonical record key.
    /// \param value Replacement bytes.
    /// \param expected_revision Transaction-visible revision to replace.
    /// \return Candidate committed revision or a typed conflict/error.
    /// \note Repeated writes in one transaction return the same candidate revision.
    [[nodiscard]] virtual Result<StorageRevision> write(
        std::string_view name_space,
        std::string_view key,
        std::string_view value,
        StorageRevision expected_revision) = 0;
    /// \brief Stages deletion of a visible record.
    /// \param name_space Namespace containing the record.
    /// \param key Canonical record key.
    /// \param expected_revision Transaction-visible revision to erase.
    /// \return Success when the revision matches, otherwise a typed error.
    [[nodiscard]] virtual Result<void> erase(
        std::string_view name_space,
        std::string_view key,
        StorageRevision expected_revision) = 0;
    /// \brief Atomically publishes the write set or returns a terminal outcome.
    /// \return `Unknown` means publication cannot be inferred and must be resolved by reading state.
    [[nodiscard]] virtual CommitResult commit() = 0;
    /// \brief Rolls back only an active transaction owned by the calling thread.
    virtual void rollback() noexcept = 0;
};

/// \brief Backend-neutral owner and factory for storage transactions.
/// \thread_safety Implementations document concurrency limits; the reference backend is concurrent.
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    /// \brief Begins a transaction when schema metadata permits admission.
    /// \param mode Requested read-only or read-write mode.
    /// \return A transaction borrowing this backend, or a schema/admission error.
    /// \note The backend must outlive the returned transaction.
    [[nodiscard]] virtual Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) = 0;
    /// \brief Returns the currently configured schema metadata.
    /// \return Schema or a backend access error.
    [[nodiscard]] virtual Result<StorageSchema> schema() const = 0;
    /// \brief Validates backend metadata against an expected schema.
    /// \param expected Required profile, namespace versions and migration state.
    /// \return Success only when the schema is compatible.
    [[nodiscard]] virtual Result<void> validate_schema(const StorageSchema& expected) const = 0;
};

/// \brief Move-only RAII owner for one backend transaction.
class UnitOfWork {
public:
    UnitOfWork() noexcept = default;
    /// \brief Takes exclusive ownership of a backend transaction.
    /// \param transaction Transaction to own; null creates an invalid unit.
    explicit UnitOfWork(std::unique_ptr<ITransaction> transaction) noexcept
        : m_transaction(std::move(transaction))
    {
    }
    UnitOfWork(const UnitOfWork&) = delete;
    UnitOfWork& operator=(const UnitOfWork&) = delete;
    /// \brief Transfers transaction ownership.
    UnitOfWork(UnitOfWork&&) noexcept = default;
    /// Replaces ownership after rolling back the currently owned active transaction.
    /// \param other Unit whose transaction ownership is transferred.
    /// \return This unit after the transfer.
    UnitOfWork& operator=(UnitOfWork&& other) noexcept;
    ~UnitOfWork();

    /// \brief Begins and owns a transaction borrowed from `backend`.
    /// \param backend Backend that must outlive the returned unit.
    /// \param mode Requested transaction mode.
    /// \return Owning unit or a backend admission error.
    [[nodiscard]] static Result<UnitOfWork> begin(IStorageBackend& backend,
                                                  TransactionMode mode);
    /// \brief Reports whether this unit owns a transaction.
    /// \return `true` when `transaction()` is non-null.
    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(m_transaction); }
    /// \brief Returns the mutable owned transaction, or null when invalid.
    /// \return Mutable borrowed pointer or null.
    [[nodiscard]] ITransaction* transaction() noexcept { return m_transaction.get(); }
    /// \brief Returns the const owned transaction, or null when invalid.
    /// \return Const borrowed pointer or null.
    [[nodiscard]] const ITransaction* transaction() const noexcept
    {
        return m_transaction.get();
    }
    /// \brief Commits the owned transaction; `Unknown` remains terminal and is not retried.
    /// \return Terminal commit result or an invalid-state failure.
    [[nodiscard]] CommitResult commit();
    /// Rolls back an active transaction. Unknown outcomes are never rolled back.
    void rollback() noexcept;

private:
    std::unique_ptr<ITransaction> m_transaction;
};

/// \brief One-shot deterministic fault controls for storage conformance tests.
struct InMemoryFailureInjection {
    bool fail_write_set_preparation = false; ///< Fail while preparing a copied write set.
    bool fail_before_publish = false; ///< Return `Failed` before publication.
    bool unknown_before_publish = false; ///< Return `Unknown` without publishing.
    bool unknown_after_publish = false; ///< Publish and then return `Unknown`.
};

/// \brief Deterministic snapshot/optimistic backend used as the M2 semantic baseline.
/// \details Supports multiple active optimistic writers. Deleted keys retain a generation
/// tombstone for the backend lifetime so stale pre-delete transactions cannot overwrite a
/// recreated record. This backend is not durable.
class InMemoryStorageBackend final : public IStorageBackend {
public:
    /// \brief Creates a process-local backend with fixed schema metadata.
    /// \param schema Schema governing transaction admission.
    explicit InMemoryStorageBackend(StorageSchema schema = StorageSchema{1, {}});
    ~InMemoryStorageBackend() override;

    /// \copydoc IStorageBackend::begin_transaction
    [[nodiscard]] Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) override;
    /// \copydoc IStorageBackend::schema
    [[nodiscard]] Result<StorageSchema> schema() const override;
    /// \copydoc IStorageBackend::validate_schema
    [[nodiscard]] Result<void> validate_schema(const StorageSchema& expected) const override;

    /// \brief Installs one-shot failures for deterministic tests.
    /// \param injection Failure points consumed by subsequent real writes.
    void set_failure_injection(InMemoryFailureInjection injection);
    /// \brief Returns the number of currently visible records.
    /// \return Count of records with visible values.
    /// \thread_safety Safe to call concurrently.
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
