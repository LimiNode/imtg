#include <imtg/actions.hpp>
#include <imtg/core.hpp>
#include <imtg/runtime.hpp>
#include <imtg/reconciliation.hpp>
#include <imtg/storage.hpp>
#include <imtg/sessions.hpp>
#include <imtg/ui.hpp>

int main()
{
    const imtg::BotId bot{1};
    imtg::InMemoryStorageBackend backend;
    auto unit = imtg::UnitOfWork::begin(backend, imtg::TransactionMode::ReadWrite);
    if (bot.value != 1 || !unit) return 1;
    imtg::OutboxRepository outbox;
    const auto page = outbox.claim_outbox_effects(
        1, imtg::ClockTime{}, std::chrono::seconds(1), {}, *unit.value().transaction());
    return page && page.value().effects.empty()
        && unit.value().commit().outcome == imtg::CommitOutcome::Committed ? 0 : 1;
}
