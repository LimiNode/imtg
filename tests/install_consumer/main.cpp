#include <imtg/actions/actions.hpp>
#include <imtg/core/core.hpp>
#include <imtg/runtime/runtime.hpp>
#include <imtg/storage/storage.hpp>
#include <imtg/ui/ui.hpp>

int main()
{
    const imtg::BotId bot{1};
    imtg::InMemoryStorageBackend backend;
    auto unit = imtg::UnitOfWork::begin(backend, imtg::TransactionMode::ReadOnly);
    return bot.value == 1 && unit ? 0 : 1;
}
