#include <imtg/storage/mdbx_storage.hpp>

#include <filesystem>
#include <utility>

int main()
{
    const auto path = std::filesystem::current_path() / "provider-test.mdbx";
    imtg::MdbxStorageConfig config;
    config.path = path.string();
    config.schema = imtg::StorageSchema{1, {{"sessions", 1}}};
    auto opened = imtg::MdbxStorageBackend::open(std::move(config));
    if (!opened) return 1;
    auto backend = std::move(opened).value();
    auto started = imtg::UnitOfWork::begin(*backend, imtg::TransactionMode::ReadWrite);
    if (!started) return 2;
    auto work = std::move(started).value();
    const auto original = work.transaction()->read("sessions", "provider");
    if (!original) return 3;
    const auto staged = original.value()
        ? work.transaction()->write("sessions", "provider", "ready", original.value()->revision)
        : work.transaction()->create_if_absent("sessions", "provider", "ready");
    if (!staged || work.commit().outcome != imtg::CommitOutcome::Committed) return 4;
    auto reading = imtg::UnitOfWork::begin(*backend, imtg::TransactionMode::ReadOnly);
    if (!reading) return 5;
    auto read = std::move(reading).value();
    const auto restored = read.transaction()->read("sessions", "provider");
    if (!restored || !restored.value() || restored.value()->value != "ready") return 6;
    if (read.commit().outcome != imtg::CommitOutcome::Committed || !backend->close()) return 7;
    std::filesystem::remove(path);
    return 0;
}
