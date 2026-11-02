#include "imtg/sessions/render_intents.hpp"

#include "test_support.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
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

imtg::RenderIntentId append_always_intent(imtg::IStorageBackend& backend,
                                          const imtg::ConversationKey& key,
                                          imtg::StorageRevision revision,
                                          std::uint64_t update_id)
{
    imtg::RenderIntentRepository repository;
    const imtg::UpdateIdentity origin{key.bot_id, update_id};
    const auto id = repository.make_id(key, revision, origin);
    auto work = begin(backend);
    CHECK(repository.append(imtg::RenderIntent{id, key, revision, "screen", origin},
                            *work.transaction()));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    return id;
}

void append_u32(std::string& output, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

void append_u64(std::string& output, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

imtg::RenderIntentId append_legacy_intent(imtg::IStorageBackend& backend,
                                          const imtg::ConversationKey& key,
                                          imtg::StorageRevision revision)
{
    const auto id = imtg::RenderIntentRepository::make_id(key, revision);
    std::string value = key.encode();
    value.push_back('\0');
    append_u64(value, revision);
    const std::string metadata = "legacy-screen";
    append_u32(value, static_cast<std::uint32_t>(metadata.size()));
    value += metadata;
    value.push_back('\0');
    auto work = begin(backend);
    CHECK(work.transaction()->create_if_absent("render_intents", id.value, value));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    return id;
}

void store_claim_bytes(imtg::IStorageBackend& backend, const imtg::RenderIntentId& id,
                       std::string bytes)
{
    auto work = begin(backend);
    CHECK(work.transaction()->create_if_absent("render_intent_claims", id.value, bytes));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
}

std::string pending_claim_at(std::int64_t next_eligible_millis)
{
    std::string value{"v1", 2};
    value.push_back(static_cast<char>(imtg::RenderIntentLifecycleState::Pending));
    append_u64(value, 0);
    append_u64(value, 0);
    append_u64(value, static_cast<std::uint64_t>(next_eligible_millis));
    value.push_back('\0');
    return value;
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
    const auto staged = claims.claim_render_intents(1, now, 1s, {}, *unknown.transaction());
    CHECK(staged && staged.value().claims.size() == 1 && staged.value().claims.front().id == id2);
    CHECK(unknown.commit().outcome == imtg::CommitOutcome::Unknown);

    auto inspect = begin(backend, imtg::TransactionMode::ReadOnly);
    const auto recovered = claims.inspect(id2, now, *inspect.transaction());
    CHECK(recovered && recovered.value() && recovered.value()->state == imtg::RenderIntentLifecycleState::Pending
          && recovered.value()->claim_token.value == 0);
    CHECK(inspect.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_claim_unknown_after_publication_recovery()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto id = append_intent(backend, conversation(801, 901), 1);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{5000ms};

    imtg::InMemoryFailureInjection injection;
    injection.unknown_after_publish = true;
    backend.set_failure_injection(injection);
    auto first = begin(backend);
    const auto staged = claims.claim_render_intents(
        1, now, 100ms, {}, *first.transaction());
    CHECK(staged && staged.value().claims.size() == 1);
    const auto original = staged.value().claims.front();
    CHECK(first.commit().outcome == imtg::CommitOutcome::Unknown);

    auto inspect = begin(backend, imtg::TransactionMode::ReadOnly);
    const auto recovered = claims.inspect(id, now, *inspect.transaction());
    CHECK(recovered && recovered.value()
          && recovered.value()->state == imtg::RenderIntentLifecycleState::Claimed
          && recovered.value()->claim_token.value == original.claim_token.value
          && recovered.value()->row_revision == original.row_revision
          && recovered.value()->lease_until == original.lease_until);
    CHECK(inspect.commit().outcome == imtg::CommitOutcome::Committed);

    auto competing = begin(backend);
    const auto blocked = claims.claim_render_intents(
        1, now, 100ms, {}, *competing.transaction());
    CHECK(blocked && blocked.value().claims.empty());
    competing.rollback();

    auto reclaim = begin(backend);
    const auto reclaimed = claims.claim_render_intents(
        1, now + 101ms, 100ms, {}, *reclaim.transaction());
    CHECK(reclaimed && reclaimed.value().claims.size() == 1
          && reclaimed.value().claims.front().claim_token.value > original.claim_token.value);
    const auto replacement = reclaimed.value().claims.front();
    CHECK(reclaim.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(backend);
    const auto stale_completion = claims.complete_render_intent(
        id, original.claim_token, original.row_revision, now + 101ms,
        *stale.transaction());
    CHECK(!stale_completion && stale_completion.error().code == imtg::ErrorCode::StorageStaleClaim);
    stale.rollback();

    auto complete = begin(backend);
    const auto completed = claims.complete_render_intent(
        id, replacement.claim_token, replacement.row_revision, now + 101ms,
        *complete.transaction());
    CHECK(completed && complete.commit().outcome == imtg::CommitOutcome::Committed);
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

void test_partial_page_continuation_and_budget_progress()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(90, 120);
    const auto a = append_intent(backend, key, 1);
    const auto b = append_intent(backend, key, 2);
    const auto c = append_intent(backend, key, 3);
    const auto d = append_intent(backend, key, 4);
    const auto e = append_intent(backend, key, 5);
    store_claim_bytes(backend, a, pending_claim_at(10'000));
    store_claim_bytes(backend, c, pending_claim_at(10'000));
    imtg::RenderIntentClaimRepository claims(3, 16);
    const auto now = imtg::ClockTime{1s};

    auto first = begin(backend);
    const auto page = claims.claim_render_intents(2, now, 1s, {}, *first.transaction());
    CHECK(page && page.value().claims.size() == 2);
    CHECK(page.value().claims[0].id == b && page.value().claims[1].id == d);
    CHECK(page.value().continuation.has_value() && page.value().examined == 4);
    const auto continuation = *page.value().continuation;
    CHECK(first.commit().outcome == imtg::CommitOutcome::Committed);

    auto second = begin(backend);
    const auto tail = claims.claim_render_intents(
        2, now, 1s, continuation, *second.transaction());
    CHECK(tail && tail.value().claims.size() == 1 && tail.value().claims.front().id == e);
    CHECK(!tail.value().continuation.has_value());
    CHECK(second.commit().outcome == imtg::CommitOutcome::Committed);

    imtg::RenderIntentClaimRepository one_at_a_time(3, 1);
    auto budgeted = begin(backend);
    const auto empty = one_at_a_time.claim_render_intents(
        1, now, 1s, {}, *budgeted.transaction());
    CHECK(empty && empty.value().claims.empty() && empty.value().examined == 1
          && empty.value().continuation.has_value());
    budgeted.rollback();

    auto invalid = begin(backend);
    const auto raw_cursor = claims.claim_render_intents(1, now, 1s, a.value,
                                                        *invalid.transaction());
    CHECK(!raw_cursor && raw_cursor.error().code == imtg::ErrorCode::UnsupportedVersion);
    invalid.rollback();

    imtg::InMemoryStorageBackend exact_backend(schema());
    append_intent(exact_backend, conversation(901, 1201), 1);
    append_intent(exact_backend, conversation(902, 1202), 1);
    imtg::RenderIntentClaimRepository exact_claims(2, 2);
    auto exact_work = begin(exact_backend);
    const auto exact = exact_claims.claim_render_intents(
        2, now, 1s, {}, *exact_work.transaction());
    CHECK(exact && exact.value().claims.size() == 2 && exact.value().continuation);
    const auto exact_continuation = *exact.value().continuation;
    CHECK(exact_work.commit().outcome == imtg::CommitOutcome::Committed);
    auto exact_end_work = begin(exact_backend);
    const auto exact_end = exact_claims.claim_render_intents(
        2, now, 1s, exact_continuation, *exact_end_work.transaction());
    CHECK(exact_end && exact_end.value().claims.empty() && !exact_end.value().continuation);
    exact_end_work.rollback();

    imtg::InMemoryStorageBackend future_backend(schema());
    const auto future = append_intent(future_backend, conversation(903, 1203), 1);
    store_claim_bytes(future_backend, future, pending_claim_at(10'000));
    auto future_work = begin(future_backend);
    const auto no_eligible = claims.claim_render_intents(
        2, now, 1s, {}, *future_work.transaction());
    CHECK(no_eligible && no_eligible.value().claims.empty()
          && !no_eligible.value().continuation && no_eligible.value().examined == 1);
    future_work.rollback();
}

void test_lease_precision_and_boundaries()
{
    imtg::InMemoryStorageBackend backend(schema());
    const auto id = append_intent(backend, conversation(91, 121), 1);
    imtg::RenderIntentClaimRepository claims;
    const auto now = imtg::ClockTime{1000ms + 900us};

    auto claim_work = begin(backend);
    const auto page = claims.claim_render_intents(1, now, 1ms, {}, *claim_work.transaction());
    CHECK(page && page.value().claims.size() == 1);
    const auto claimed = page.value().claims.front();
    CHECK(claimed.lease_until >= now + 1ms);
    CHECK(claimed.lease_until == imtg::ClockTime{1002ms});
    CHECK(claim_work.commit().outcome == imtg::CommitOutcome::Committed);

    auto renew_work = begin(backend);
    const auto requested = now + 2500us;
    const auto renewed = claims.renew_render_intent(
        id, claimed.claim_token, claimed.row_revision, now, requested,
        *renew_work.transaction());
    CHECK(renewed && renewed.value().lease_until >= requested
          && renewed.value().lease_until == imtg::ClockTime{1004ms});
    CHECK(renew_work.commit().outcome == imtg::CommitOutcome::Committed);

    auto inspect_work = begin(backend, imtg::TransactionMode::ReadOnly);
    const auto inspected = claims.inspect(id, now, *inspect_work.transaction());
    CHECK(inspected && inspected.value()
          && inspected.value()->lease_until == renewed.value().lease_until);
    CHECK(inspect_work.commit().outcome == imtg::CommitOutcome::Committed);

    imtg::InMemoryStorageBackend pre_epoch_backend(schema());
    append_intent(pre_epoch_backend, conversation(92, 122), 1);
    const auto before_epoch = imtg::ClockTime{-1000ms - 900us};
    auto pre_epoch_work = begin(pre_epoch_backend);
    const auto pre_epoch = claims.claim_render_intents(
        1, before_epoch, 1ms, {}, *pre_epoch_work.transaction());
    CHECK(pre_epoch && pre_epoch.value().claims.size() == 1
          && pre_epoch.value().claims.front().lease_until >= before_epoch + 1ms);
    pre_epoch_work.rollback();

    imtg::InMemoryStorageBackend boundary_backend(schema());
    append_intent(boundary_backend, conversation(93, 123), 1);
    auto boundary_work = begin(boundary_backend);
    const auto overflow = claims.claim_render_intents(
        1, imtg::ClockTime::max(), 1ms, {}, *boundary_work.transaction());
    CHECK(!overflow && overflow.error().code == imtg::ErrorCode::InvalidArgument);
    boundary_work.rollback();

    imtg::InMemoryStorageBackend min_backend(schema());
    append_intent(min_backend, conversation(930, 1230), 1);
    auto min_work = begin(min_backend);
    const auto minimum = claims.claim_render_intents(
        1, imtg::ClockTime::min(), 1ms, {}, *min_work.transaction());
    CHECK(minimum && minimum.value().claims.size() == 1
          && minimum.value().claims.front().lease_until
              >= imtg::ClockTime::min() + 1ms);
    min_work.rollback();

    imtg::InMemoryStorageBackend duration_backend(schema());
    append_intent(duration_backend, conversation(931, 1231), 1);
    auto duration_work = begin(duration_backend);
    const auto oversized = claims.claim_render_intents(
        1, imtg::ClockTime{}, std::chrono::milliseconds::max(), {},
        *duration_work.transaction());
    CHECK(!oversized && oversized.error().code == imtg::ErrorCode::InvalidArgument);
    duration_work.rollback();
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

void test_real_legacy_bootstrap_and_recovery()
{
    const auto key = conversation(94, 124);
    const auto now = imtg::ClockTime{100ms};
    imtg::RenderIntentClaimRepository claims(2, 8);

    imtg::InMemoryStorageBackend backend(schema());
    const auto legacy = append_legacy_intent(backend, key, 1);
    auto rejected_work = begin(backend);
    const auto rejected = claims.claim_render_intents(
        1, now, 1s, {}, *rejected_work.transaction());
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageNotFound);
    rejected_work.rollback();

    auto left = begin(backend);
    auto right = begin(backend);
    CHECK(claims.admit_legacy_intents(*left.transaction()));
    CHECK(claims.admit_legacy_intents(*right.transaction()));
    CHECK(left.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(right.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    auto claim_work = begin(backend);
    const auto admitted = claims.claim_render_intents(
        1, now, 1s, {}, *claim_work.transaction());
    CHECK(admitted && admitted.value().claims.size() == 1
          && admitted.value().claims.front().id == legacy);
    CHECK(claim_work.commit().outcome == imtg::CommitOutcome::Committed);

    imtg::InMemoryStorageBackend unknown_backend(schema());
    append_legacy_intent(unknown_backend, conversation(95, 125), 1);
    imtg::InMemoryFailureInjection injection;
    injection.unknown_after_publish = true;
    unknown_backend.set_failure_injection(injection);
    auto unknown_work = begin(unknown_backend);
    CHECK(claims.admit_legacy_intents(*unknown_work.transaction()));
    CHECK(unknown_work.commit().outcome == imtg::CommitOutcome::Unknown);
    auto authoritative = begin(unknown_backend, imtg::TransactionMode::ReadOnly);
    imtg::DesiredGenerationRepository generations;
    const auto marker = generations.load(conversation(95, 125).encode(),
                                         *authoritative.transaction());
    CHECK(marker && marker.value().value == 1);
    CHECK(authoritative.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_legacy_bootstrap_races_new_tx1_and_budget()
{
    const auto key = conversation(96, 126);
    imtg::InMemoryStorageBackend backend(schema());
    append_legacy_intent(backend, key, 1);
    imtg::RenderIntentClaimRepository claims(2, 8);
    auto bootstrap = begin(backend);
    auto tx1 = begin(backend);
    CHECK(claims.admit_legacy_intents(*bootstrap.transaction()));
    imtg::RenderIntentRepository intents;
    const auto newer_id = intents.make_id(key, 2);
    CHECK(intents.append(imtg::RenderIntent{newer_id, key, 2, "new", std::nullopt},
                         *tx1.transaction()));
    CHECK(tx1.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(bootstrap.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    auto verify = begin(backend, imtg::TransactionMode::ReadOnly);
    imtg::DesiredGenerationRepository generations;
    const auto marker = generations.load(key.encode(), *verify.transaction());
    CHECK(marker && marker.value().value == 2);
    CHECK(verify.commit().outcome == imtg::CommitOutcome::Committed);

    imtg::InMemoryStorageBackend oversized(schema());
    append_legacy_intent(oversized, conversation(97, 127), 1);
    append_legacy_intent(oversized, conversation(98, 128), 1);
    imtg::RenderIntentClaimRepository bounded(1, 1);
    auto migration = begin(oversized);
    const auto over_budget = bounded.admit_legacy_intents(*migration.transaction());
    CHECK(!over_budget && over_budget.error().code == imtg::ErrorCode::InvalidArgument);
    migration.rollback();

    auto absent = begin(oversized, imtg::TransactionMode::ReadOnly);
    CHECK(!generations.load(conversation(97, 127).encode(), *absent.transaction()));
    CHECK(absent.commit().outcome == imtg::CommitOutcome::Committed);
}

void test_malformed_legacy_and_claim_envelopes()
{
    imtg::RenderIntentClaimRepository claims;
    imtg::InMemoryStorageBackend malformed_legacy(schema());
    auto raw = begin(malformed_legacy);
    CHECK(raw.transaction()->create_if_absent("render_intents", "bad", "not-m2b"));
    CHECK(raw.commit().outcome == imtg::CommitOutcome::Committed);
    auto admission = begin(malformed_legacy);
    const auto rejected = claims.admit_legacy_intents(*admission.transaction());
    CHECK(!rejected);
    admission.rollback();

    imtg::InMemoryStorageBackend malformed_claim(schema());
    const auto id = append_intent(malformed_claim, conversation(99, 129), 1);
    std::string timestamp = pending_claim_at(std::numeric_limits<std::int64_t>::max());
    store_claim_bytes(malformed_claim, id, timestamp);
    auto inspect = begin(malformed_claim, imtg::TransactionMode::ReadOnly);
    const auto invalid_time = claims.inspect(id, imtg::ClockTime{}, *inspect.transaction());
    CHECK(!invalid_time && invalid_time.error().code == imtg::ErrorCode::InvalidEncoding);
    inspect.rollback();

    imtg::InMemoryStorageBackend minimum_claim(schema());
    const auto minimum_id = append_intent(minimum_claim, conversation(990, 1390), 1);
    store_claim_bytes(minimum_claim, minimum_id,
                      pending_claim_at(std::numeric_limits<std::int64_t>::min()));
    auto minimum_inspect = begin(minimum_claim, imtg::TransactionMode::ReadOnly);
    const auto invalid_minimum = claims.inspect(
        minimum_id, imtg::ClockTime{}, *minimum_inspect.transaction());
    CHECK(!invalid_minimum && invalid_minimum.error().code == imtg::ErrorCode::InvalidEncoding);
    minimum_inspect.rollback();

    imtg::InMemoryStorageBackend bad_version(schema());
    const auto version_id = append_intent(bad_version, conversation(100, 130), 1);
    store_claim_bytes(bad_version, version_id, "v2-bad");
    auto version_work = begin(bad_version, imtg::TransactionMode::ReadOnly);
    const auto invalid_version = claims.inspect(
        version_id, imtg::ClockTime{}, *version_work.transaction());
    CHECK(!invalid_version && invalid_version.error().code == imtg::ErrorCode::UnsupportedVersion);
    version_work.rollback();
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

void test_supersession_rejections_and_generation_fence()
{
    const auto now = imtg::ClockTime{1s};
    imtg::RenderIntentClaimRepository claims;
    imtg::InMemoryStorageBackend backend(schema());
    const auto key = conversation(101, 131);
    const auto old = append_intent(backend, key, 2);
    auto claim_work = begin(backend);
    const auto page = claims.claim_render_intents(1, now, 10s, {}, *claim_work.transaction());
    CHECK(page && page.value().claims.size() == 1);
    const auto claimed = page.value().claims.front();
    CHECK(claim_work.commit().outcome == imtg::CommitOutcome::Committed);

    const auto older = append_intent(backend, key, 1);
    const auto unrelated = append_intent(backend, conversation(101, 999), 3);
    const auto other_bot = append_intent(backend, conversation(202, 131), 3);
    for (const auto& candidate : {old, older, unrelated, other_bot}) {
        auto work = begin(backend);
        const auto result = claims.supersede_render_intent(
            old, claimed.claim_token, claimed.row_revision, now, candidate,
            *work.transaction());
        CHECK(!result);
        work.rollback();
    }

    imtg::InMemoryStorageBackend always_backend(schema());
    const auto always_key = conversation(102, 132);
    const auto first_always = append_always_intent(always_backend, always_key, 5, 1);
    auto always_claim_work = begin(always_backend);
    const auto always_page = claims.claim_render_intents(
        1, now, 10s, {}, *always_claim_work.transaction());
    CHECK(always_page && always_page.value().claims.size() == 1
          && always_page.value().claims.front().id == first_always);
    const auto always_claim = always_page.value().claims.front();
    CHECK(always_claim_work.commit().outcome == imtg::CommitOutcome::Committed);
    const auto second_always = append_always_intent(always_backend, always_key, 5, 2);
    auto same_revision = begin(always_backend);
    const auto independent = claims.supersede_render_intent(
        first_always, always_claim.claim_token, always_claim.row_revision, now,
        second_always, *same_revision.transaction());
    CHECK(!independent && independent.error().code == imtg::ErrorCode::InvalidArgument);
    same_revision.rollback();

    imtg::InMemoryStorageBackend fenced_backend(schema());
    const auto fenced_key = conversation(103, 133);
    const auto fenced_old = append_intent(fenced_backend, fenced_key, 1);
    auto fenced_claim_work = begin(fenced_backend);
    const auto fenced_page = claims.claim_render_intents(
        1, now, 10s, {}, *fenced_claim_work.transaction());
    CHECK(fenced_page && fenced_page.value().claims.size() == 1);
    const auto fenced_claim = fenced_page.value().claims.front();
    CHECK(fenced_claim_work.commit().outcome == imtg::CommitOutcome::Committed);
    const auto fenced_new = append_intent(fenced_backend, fenced_key, 2);
    auto supersede = begin(fenced_backend);
    CHECK(claims.supersede_render_intent(
        fenced_old, fenced_claim.claim_token, fenced_claim.row_revision, now,
        fenced_new, *supersede.transaction()));
    append_intent(fenced_backend, fenced_key, 3);
    CHECK(supersede.commit().outcome == imtg::CommitOutcome::RejectedConflict);
}

} // namespace

int main()
{
    test_claim_and_fencing();
    test_competing_claims_and_unknown_recovery();
    test_claim_unknown_after_publication_recovery();
    test_bounded_pagination_and_legacy_bootstrap();
    test_partial_page_continuation_and_budget_progress();
    test_lease_precision_and_boundaries();
    test_invalid_limits();
    test_desired_generation_bootstrap_and_fencing();
    test_real_legacy_bootstrap_and_recovery();
    test_legacy_bootstrap_races_new_tx1_and_budget();
    test_malformed_legacy_and_claim_envelopes();
    test_release_and_supersession();
    test_supersession_rejections_and_generation_fence();
    return 0;
}
