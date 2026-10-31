#include "imtg/sessions/render_intents.hpp"

#include "test_support.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace {

using namespace std::chrono_literals;

imtg::StorageSchema schema()
{
    return imtg::StorageSchema{1, {{"render_intents", 1}, {"render_intent_claims", 1}}};
}

imtg::ConversationKey conversation(std::uint64_t bot, std::int64_t chat)
{
    return imtg::ConversationKey{imtg::BotId{bot}, imtg::ChatId{chat}, imtg::Chat{}};
}

imtg::UnitOfWork begin(imtg::IStorageBackend& backend,
                       imtg::TransactionMode mode = imtg::TransactionMode::ReadWrite)
{
    auto result = imtg::UnitOfWork::begin(backend, mode);
    CHECK(result);
    return std::move(result).value();
}

imtg::RenderIntentId append_intent(imtg::IStorageBackend& backend,
                                   const imtg::ConversationKey& key,
                                   imtg::StorageRevision revision)
{
    imtg::RenderIntentRepository repository;
    auto work = begin(backend);
    const auto id = repository.make_id(key, revision);
    const auto appended = repository.append(
        imtg::RenderIntent{id, key, revision, "screen", std::nullopt}, *work.transaction());
    CHECK(appended);
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    return id;
}

void test_claim_and_fencing()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(7, -42);
    const auto id = append_intent(backend, key, 1);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{std::chrono::milliseconds{1000}};

    auto first = begin(backend);
    const auto page = claims.claim_render_intents(1, now, 100ms, {}, *first.transaction());
    CHECK(page && page.value().claims.size() == 1);
    const auto claimed = page.value().claims.front();
    CHECK(claimed.id == id && claimed.state == imtg::RenderIntentLifecycleState::Claimed
          && claimed.claim_token.value == 1 && claimed.row_revision != 0);
    CHECK(first.commit().outcome == imtg::CommitOutcome::Committed);

    auto renew = begin(backend);
    const auto renewed = claims.renew_render_intent(
        id, claimed.claim_token, claimed.row_revision, now, now + 200ms, *renew.transaction());
    CHECK(renewed && renewed.value().lease_until == now + 200ms);
    CHECK(renew.commit().outcome == imtg::CommitOutcome::Committed);

    auto competing = begin(backend);
    const auto competing_page = claims.claim_render_intents(
        1, now + 300ms, 100ms, {}, *competing.transaction());
    CHECK(competing_page && competing_page.value().claims.size() == 1);
    const auto second = competing_page.value().claims.front();
    CHECK(second.claim_token.value == 2);
    CHECK(competing.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(backend);
    const auto rejected = claims.complete_render_intent(
        id, claimed.claim_token, claimed.row_revision, now + 300ms, *stale.transaction());
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageStaleClaim);
    stale.rollback();

    auto complete = begin(backend);
    const auto completed = claims.complete_render_intent(
        id, second.claim_token, second.row_revision, now + 300ms, *complete.transaction());
    CHECK(completed && completed.value().state == imtg::RenderIntentLifecycleState::Completed);
    CHECK(complete.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_competing_claims_and_unknown_recovery()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(8, 11);
    const auto id = append_intent(backend, key, 1);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{std::chrono::milliseconds{10}};

    auto left = begin(backend);
    auto right = begin(backend);
    const auto left_page = claims.claim_render_intents(1, now, 1s, {}, *left.transaction());
    const auto right_page = claims.claim_render_intents(1, now, 1s, {}, *right.transaction());
    CHECK(left_page && right_page);
    CHECK(left.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(right.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    const auto id2 = append_intent(backend, key, 2);
    imtg::InMemoryFailureInjection injection;
    injection.unknown_before_publish = true;
    backend.set_failure_injection(injection);
    auto unknown = begin(backend);
    const auto staged = claims.claim_render_intents(1, now, 1s, id.value, *unknown.transaction());
    CHECK(staged && staged.value().claims.size() == 1 && staged.value().claims.front().id == id2);
    CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);

    auto inspect = begin(backend, imtg::TransactionMode::ReadOnly);
    const auto recovered = claims.inspect(id2, now, *inspect.transaction());
    CHECK(recovered && recovered.value() && recovered.value()->state == imtg::RenderIntentLifecycleState::Pending
          && recovered.value()->claim_token.value == 0);
    CHECK(inspect.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_bounded_pagination_and_legacy_bootstrap()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(9, 12);
    append_intent(backend, key, 1);
    append_intent(backend, key, 2);
    append_intent(backend, key, 3);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{std::chrono::milliseconds{100}};
    std::string continuation;
    std::size_t count = 0;
    for (;;) {
        auto work = begin(backend);
        const auto page = claims.claim_render_intents(1, now, 1s, continuation, *work.transaction());
        CHECK(page && page.value().claims.size() == 1);
        ++count;
        continuation = page.value().continuation.value_or(std::string{});
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        if (count == 3) break;
    }
    CHECK(count == 3);

    auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
    const auto first = claims.inspect(imtg::RenderIntentRepository::make_id(key, 1), now,
                                      *verify.transaction());
    CHECK(first && first.value() && first.value()->state == imtg::RenderIntentLifecycleState::Claimed);
    CHECK(verify.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_invalid_limits()
{
    imtg::InMemoryStorageBackend backend(schema());
    imtg::RenderIntentClaimRepository claims;
    auto work = begin(backend);
    const auto result = claims.claim_render_intents(
        0, imtg::ClockTime{}, 1s, {}, *work.transaction());
    CHECK(!result && result.error().code == imtg::ErrorCode::InvalidArgument);
    work.rollback();
}

void test_desired_generation_bootstrap_and_fencing()
{
    imtg::InMemoryStorageBackend backend(schema());
    imtg::DesiredGenerationRepository generations;

    auto left = begin(backend);
    auto right = begin(backend);
    const auto first = generations.ensure("scope", *left.transaction());
    const auto racing = generations.ensure("scope", *right.transaction());
    CHECK(first && racing && first.value().value == 1 && racing.value().value == 1);
    CHECK(left.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(right.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    auto advance = begin(backend);
    const auto next = generations.advance("scope", first.value().value,
                                          first.value().row_revision, *advance.transaction());
    CHECK(next && next.value().value == 2);
    CHECK(advance.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(backend);
    const auto rejected = generations.advance("scope", first.value().value,
                                              first.value().row_revision, *stale.transaction());
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageConflict);
    stale.rollback();
}

void test_release_and_supersession()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(10, 13);
    const auto id = append_intent(backend, key, 1);
    const auto newer_id = append_intent(backend, key, 2);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{std::chrono::milliseconds{1000}};

    auto claim_work = begin(backend);
    const auto claimed = claims.claim_render_intents(1, now, 1s, {}, *claim_work.transaction());
    CHECK(claimed && claimed.value().claims.size() == 1);
    const auto first = claimed.value().claims.front();
    CHECK(claim_work.commit().outcome == imtg::CommitOutcome::Committed);

    auto release_work = begin(backend);
    const auto released = claims.release_render_intent(
        id, first.claim_token, first.row_revision, now, now + 500ms, *release_work.transaction());
    CHECK(released && released.value().state == imtg::RenderIntentLifecycleState::Pending);
    CHECK(release_work.commit().outcome == imtg::CommitOutcome::Committed);

    auto reclaim_work = begin(backend);
    const auto reclaimed = claims.claim_render_intents(
        1, now + 600ms, 1s, {}, *reclaim_work.transaction());
    CHECK(reclaimed && reclaimed.value().claims.size() == 1);
    const auto second = reclaimed.value().claims.front();
    CHECK(second.claim_token.value == 2);
    CHECK(reclaim_work.commit().outcome == imtg::CommitOutcome::Committed);

    auto supersede_work = begin(backend);
    const auto superseded = claims.supersede_render_intent(
        id, second.claim_token, second.row_revision, now + 600ms, newer_id,
        *supersede_work.transaction());
    CHECK(superseded && superseded.value().state == imtg::RenderIntentLifecycleState::Superseded
          && superseded.value().superseded_by && superseded.value().superseded_by->value == newer_id.value);
    CHECK(supersede_work.commit().outcome == imtg::CommitOutcome::Committed);
}

} // namespace

int main()
{
    test_claim_and_fencing();
    test_competing_claims_and_unknown_recovery();
    test_bounded_pagination_and_legacy_bootstrap();
    test_invalid_limits();
    test_desired_generation_bootstrap_and_fencing();
    test_release_and_supersession();
    return 0;
}
