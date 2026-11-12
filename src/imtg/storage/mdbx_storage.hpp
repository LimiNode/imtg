#ifndef IMTG_STORAGE_MDBX_STORAGE_HPP
#define IMTG_STORAGE_MDBX_STORAGE_HPP

#include "imtg/storage/storage.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace imtg {

/// \brief Configuration used when opening the optional MDBX storage adapter.
struct MdbxStorageConfig {
    std::string path; ///< MDBX file or directory path.
    StorageSchema schema{1, {}}; ///< Schema written when the store is created.
    std::int64_t max_dbs = 8; ///< Named-DBI capacity, including reserved headroom.
    std::int64_t max_readers = 0; ///< MDBX reader slots; zero selects the MDBX default.
    bool read_only = false; ///< Opens an existing environment without writes.
    bool no_subdir = true; ///< Stores the environment in one file when true.
    bool sync_durable = true; ///< Requests durable MDBX commits when true.
};

/// \brief Persistent MDBX implementation of the backend-neutral storage protocol.
///
/// The adapter keeps MDBX types in its implementation target. Transactions are
/// thread-affine, use one consistent MDBX snapshot, retain tombstone revisions,
/// and map uncertain native commit errors to `CommitOutcome::Unknown`.
/// The six physical DBIs are `meta`, `sessions`, `processed_updates`,
/// `render_intents`, `outbox`, and `event_journal`. Logical dispatch-target and
/// applied-target records share the bounded `outbox` DBI through key prefixes.
class MdbxStorageBackend final : public IStorageBackend {
public:
    /// \brief Opens or creates an MDBX environment.
    /// \param config Environment path, schema and MDBX lifecycle settings.
    /// \return An opened backend or a typed storage error.
    [[nodiscard]] static Result<std::unique_ptr<MdbxStorageBackend>> open(
        MdbxStorageConfig config);

    ~MdbxStorageBackend() override;

    MdbxStorageBackend(const MdbxStorageBackend&) = delete;
    MdbxStorageBackend& operator=(const MdbxStorageBackend&) = delete;
    MdbxStorageBackend(MdbxStorageBackend&&) = delete;
    MdbxStorageBackend& operator=(MdbxStorageBackend&&) = delete;

    /// \brief Closes the environment after all transactions have ended.
    /// \return Success, or `StorageUnavailable` when MDBX still has handles.
    [[nodiscard]] Result<void> close();

    /// \brief Reports whether the MDBX environment is open.
    /// \return `true` until `close()` succeeds.
    [[nodiscard]] bool is_open() const noexcept;

    /// \copydoc IStorageBackend::begin_transaction
    [[nodiscard]] Result<std::unique_ptr<ITransaction>> begin_transaction(
        TransactionMode mode) override;
    /// \copydoc IStorageBackend::schema
    [[nodiscard]] Result<StorageSchema> schema() const override;
    /// \copydoc IStorageBackend::validate_schema
    [[nodiscard]] Result<void> validate_schema(const StorageSchema& expected) const override;

private:
    class Impl;

    explicit MdbxStorageBackend(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace imtg

#endif // IMTG_STORAGE_MDBX_STORAGE_HPP
