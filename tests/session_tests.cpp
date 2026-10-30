#include "imtg/sessions/session.hpp"

#include "test_support.hpp"

#include <chrono>
#include <limits>
#include <memory>

namespace {

using namespace std::chrono_literals;

imtg::StorageSchema schema()
{
    return imtg::StorageSchema{1, {{"sessions", 1}, {"processed_updates", 1},
                                   {"render_intents", 1}}};
}

imtg::ConversationKey conversation(std::uint64_t bot, std::int64_t chat,
                                   std::optional<std::uint64_t> user = std::nullopt)
{
    if (user) {
        return imtg::ConversationKey{imtg::BotId{bot}, imtg::ChatId{chat},
                                     imtg::UserInChat{imtg::UserId{*user}}};
    }
    return imtg::ConversationKey{imtg::BotId{bot}, imtg::ChatId{chat}, imtg::Chat{}};
}

imtg::SessionRecord initial_session(const imtg::ConversationKey& key,
                                    imtg::ClockTime now)
{
    return imtg::SessionRecord{key, 1, {"start", 1}, {"start", 1}, 0,
                               std::nullopt, std::nullopt, now, now};
}

imtg::UnitOfWork begin(imtg::IStorageBackend& backend)
{
    auto result = imtg::UnitOfWork::begin(backend, imtg::TransactionMode::ReadWrite);
    CHECK(result);
    return std::move(result).value();
}

imtg::Fsm make_fsm()
{
    imtg::Fsm fsm;
    CHECK(fsm.add_transition({"start", 1}, {"begin", 1}, {"awaiting_email", 1}));
    CHECK(fsm.add_transition({"awaiting_email", 1}, {"email", 1}, {"awaiting_consent", 1},
                             [](const imtg::FsmStateId&, const imtg::FsmEvent& event) {
                                 return !event.payload.empty();
                             }));
    CHECK(fsm.add_transition({"awaiting_consent", 1}, {"consent", 1}, {"completed", 1}));
    CHECK(fsm.add_transition({"awaiting_consent", 1}, {"refresh", 1}, {"awaiting_consent", 1}));
    CHECK(fsm.add_transition({"awaiting_email", 1}, {"cancel", 1}, {"cancelled", 1}));
    CHECK(fsm.add_transition({"awaiting_email", 1}, {"timeout", 1}, {"expired", 1}));
    CHECK(fsm.mark_terminal({"completed", 1}));
    CHECK(fsm.mark_terminal({"cancelled", 1}));
    CHECK(fsm.mark_terminal({"expired", 1}));
    return fsm;
}

void test_fsm_and_encoding()
{
    const auto key = conversation(7, -42, 99);
    const auto now = imtg::ClockTime{std::chrono::milliseconds{1234}};
    imtg::SessionRecord record{key, 1, {"awaiting_email", 1}, {"registration", 2}, 8,
                               imtg::PendingInput{"email", 1}, now + 5s, now, now + 1s};
    const auto encoded = imtg::encode_session_record(record);
    CHECK(encoded);
    const auto decoded = imtg::decode_session_record(encoded.value());
    CHECK(decoded && decoded.value().conversation == record.conversation
          && decoded.value().state == record.state
          && decoded.value().screen == record.screen
          && decoded.value().revision == record.revision
          && decoded.value().pending_input == record.pending_input
          && decoded.value().deadline == record.deadline
          && decoded.value().created_at == record.created_at
          && decoded.value().last_transition_at == record.last_transition_at);
    auto extreme = encoded.value();
    for (std::size_t i = extreme.size() - 8; i < extreme.size(); ++i) extreme[i] = '\xff';
    extreme.back() = '\x7f';
    CHECK(!imtg::decode_session_record(extreme));
    const auto before_epoch = imtg::SessionRecord{key, 1, {"awaiting_email", 1}, {"registration", 2}, 8,
        std::nullopt, std::nullopt, imtg::ClockTime{std::chrono::milliseconds{-1}},
        imtg::ClockTime{std::chrono::milliseconds{-1}}};
    const auto before_encoded = imtg::encode_session_record(before_epoch);
    CHECK(before_encoded && imtg::decode_session_record(before_encoded.value()));
    const auto modern = imtg::ClockTime{std::chrono::milliseconds{1793318400000LL}};
    auto modern_record = record;
    modern_record.created_at = modern;
    modern_record.last_transition_at = modern;
    const auto modern_encoded = imtg::encode_session_record(modern_record);
    const auto modern_decoded = modern_encoded ? imtg::decode_session_record(modern_encoded.value())
                                               : imtg::Result<imtg::SessionRecord>{modern_encoded.error()};
    CHECK(modern_decoded && modern_decoded.value().created_at == modern);
    auto corrupt = encoded.value();
    corrupt[0] = 'X';
    CHECK(!imtg::decode_session_record(corrupt));
    corrupt = encoded.value().substr(0, encoded.value().size() - 1);
    CHECK(!imtg::decode_session_record(corrupt));
    corrupt = encoded.value();
    corrupt[4] = '\x02';
    CHECK(!imtg::decode_session_record(corrupt)
          && imtg::decode_session_record(corrupt).error().code == imtg::ErrorCode::UnsupportedVersion);

    const auto fsm = make_fsm();
    const auto denied = fsm.step({"awaiting_email", 1}, {imtg::FsmEventId{"email", 1},
                                                               imtg::FsmEventKind::Normal, {}});
    CHECK(!denied && denied.error().code == imtg::ErrorCode::DispatchRejected);
    const auto accepted = fsm.step({"awaiting_email", 1},
                                   {imtg::FsmEventId{"email", 1}, imtg::FsmEventKind::Normal,
                                    "person@example.test"});
    const imtg::FsmStateId awaiting_consent{"awaiting_consent", 1};
    CHECK(accepted && accepted.value().state == awaiting_consent);
    const auto cancelled = fsm.step({"awaiting_email", 1}, imtg::FsmEvent::cancellation());
    const imtg::FsmStateId cancelled_state{"cancelled", 1};
    CHECK(cancelled && cancelled.value().state == cancelled_state);
    const auto timed_out = fsm.step({"awaiting_email", 1}, imtg::FsmEvent::timeout());
    const imtg::FsmStateId expired_state{"expired", 1};
    CHECK(timed_out && timed_out.value().state == expired_state);
    const auto terminal = fsm.step({"completed", 1}, {imtg::FsmEventId{"anything", 1},
                                                           imtg::FsmEventKind::Normal, {}});
    CHECK(!terminal && terminal.error().code == imtg::ErrorCode::InvalidState);
    const auto spoof_timeout = fsm.step({"awaiting_email", 1},
        {imtg::FsmEventId{"timeout", 1}, imtg::FsmEventKind::Normal, {}});
    CHECK(!spoof_timeout);
    const auto spoof_cancel = fsm.step({"awaiting_email", 1},
        {imtg::FsmEventId{"email", 1}, imtg::FsmEventKind::Cancellation, {}});
    CHECK(!spoof_cancel);
    const auto malformed_timeout = fsm.step({"awaiting_email", 1},
        {imtg::FsmEventId{"email", 1}, imtg::FsmEventKind::Timeout, {}});
    CHECK(!malformed_timeout);
}

void test_repositories()
{
    imtg::InMemoryStorageBackend backend(schema());
    imtg::SessionRepository sessions;
    imtg::ProcessedUpdateRepository processed;
    imtg::RenderIntentRepository intents;
    const auto key = conversation(1, 2);
    const auto now = imtg::ClockTime{std::chrono::milliseconds{100}};

    auto work = begin(backend);
    auto created = sessions.create(initial_session(key, now), *work.transaction());
    CHECK(created && created.value().revision == 1);
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);

    auto read = begin(backend);
    const auto loaded = sessions.load(key, *read.transaction());
    CHECK(loaded && loaded.value() && loaded.value()->revision == 1);
    auto changed = *loaded.value();
    changed.state = {"awaiting_email", 1};
    const auto saved = sessions.save(changed, 1, *read.transaction());
    CHECK(saved && saved.value().revision == 2);
    CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(backend);
    auto stale_record = *sessions.load(key, *stale.transaction()).value();
    auto writer = begin(backend);
    auto writer_record = *sessions.load(key, *writer.transaction()).value();
    writer_record.state = {"awaiting_consent", 1};
    CHECK(sessions.save(writer_record, 2, *writer.transaction()));
    CHECK(writer.commit().outcome == imtg::CommitOutcome::Committed);
    stale_record.state = {"start", 1};
    CHECK(sessions.save(stale_record, 2, *stale.transaction()));
    CHECK(stale.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    auto deleting = begin(backend);
    CHECK(sessions.erase(key, 3, *deleting.transaction()));
    CHECK(deleting.commit().outcome == imtg::CommitOutcome::Committed);
    auto recreating = begin(backend);
    auto recreated = initial_session(key, now);
    CHECK(sessions.create(recreated, *recreating.transaction()));
    CHECK(recreating.commit().outcome == imtg::CommitOutcome::Committed);

    auto fenced_a = begin(backend);
    const auto fenced_loaded = sessions.load(key, *fenced_a.transaction());
    CHECK(fenced_loaded && fenced_loaded.value());
    CHECK(fenced_a.transaction()->watch("sessions", key.encode(), fenced_loaded.value()->revision));
    auto fenced_b = begin(backend);
    auto fenced_record = *sessions.load(key, *fenced_b.transaction()).value();
    fenced_record.screen = {"changed", 1};
    CHECK(sessions.save(fenced_record, fenced_record.revision, *fenced_b.transaction()));
    CHECK(fenced_b.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(processed.record_if_new({imtg::BotId{1}, 900}, *fenced_a.transaction()));
    CHECK(fenced_a.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    auto watch_only = imtg::UnitOfWork::begin(backend, imtg::TransactionMode::ReadOnly);
    CHECK(watch_only);
    const auto watch_snapshot = sessions.load(key, *watch_only.value().transaction());
    CHECK(watch_snapshot && watch_snapshot.value());
    CHECK(watch_only.value().transaction()->watch("sessions", key.encode(),
                                                   watch_snapshot.value()->revision));
    CHECK(watch_only.value().commit().outcome == imtg::CommitOutcome::Committed);

    auto watch_conflict = begin(backend);
    const auto conflict_snapshot = sessions.load(key, *watch_conflict.transaction());
    CHECK(conflict_snapshot && conflict_snapshot.value());
    CHECK(watch_conflict.transaction()->watch("sessions", key.encode(),
                                               conflict_snapshot.value()->revision));
    auto watch_writer = begin(backend);
    auto watch_changed = *sessions.load(key, *watch_writer.transaction()).value();
    watch_changed.screen = {"watch-writer", 1};
    CHECK(sessions.save(watch_changed, watch_changed.revision, *watch_writer.transaction()));
    CHECK(watch_writer.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(watch_conflict.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    const auto watch_key = conversation(10, 11);
    auto watch_create = begin(backend);
    CHECK(sessions.create(initial_session(watch_key, now), *watch_create.transaction()));
    CHECK(watch_create.commit().outcome == imtg::CommitOutcome::Committed);

    // Watch before a write and watch after a write fence the same original generation.
    auto watch_before = begin(backend);
    auto before_record = *sessions.load(watch_key, *watch_before.transaction()).value();
    CHECK(watch_before.transaction()->watch("sessions", watch_key.encode(), before_record.revision));
    before_record.screen = {"before", 1};
    CHECK(sessions.save(before_record, before_record.revision, *watch_before.transaction()));
    CHECK(watch_before.commit().outcome == imtg::CommitOutcome::Committed);

    auto watch_after = begin(backend);
    auto after_record = *sessions.load(watch_key, *watch_after.transaction()).value();
    CHECK(sessions.save(after_record, after_record.revision, *watch_after.transaction()));
    const auto after_candidate = watch_after.transaction()->read("sessions", watch_key.encode());
    CHECK(after_candidate && after_candidate.value());
    CHECK(watch_after.transaction()->watch("sessions", watch_key.encode(),
                                            after_candidate.value()->revision));
    CHECK(watch_after.commit().outcome == imtg::CommitOutcome::Committed);

    const auto create_key = conversation(10, 12);
    auto watch_after_create = begin(backend);
    const auto created_candidate = sessions.create(initial_session(create_key, now),
                                                     *watch_after_create.transaction());
    CHECK(created_candidate);
    CHECK(watch_after_create.transaction()->watch("sessions", create_key.encode(),
                                                   created_candidate.value().revision));
    CHECK(watch_after_create.commit().outcome == imtg::CommitOutcome::Committed);

    const auto erase_key = conversation(10, 13);
    auto erase_create = begin(backend);
    CHECK(sessions.create(initial_session(erase_key, now), *erase_create.transaction()));
    CHECK(erase_create.commit().outcome == imtg::CommitOutcome::Committed);
    auto watch_after_erase = begin(backend);
    const auto erase_record = sessions.load(erase_key, *watch_after_erase.transaction());
    CHECK(erase_record && erase_record.value());
    CHECK(sessions.erase(erase_key, erase_record.value()->revision, *watch_after_erase.transaction()));
    const auto erased_visible = watch_after_erase.transaction()->read("sessions", erase_key.encode());
    CHECK(erased_visible && !erased_visible.value());
    CHECK(watch_after_erase.transaction()->watch("sessions", erase_key.encode(),
                                                  erase_record.value()->revision + 1));
    CHECK(watch_after_erase.commit().outcome == imtg::CommitOutcome::Committed);

    auto marker = begin(backend);
    CHECK(processed.record_if_new({imtg::BotId{1}, 44}, *marker.transaction()));
    const auto duplicate = processed.record_if_new({imtg::BotId{1}, 44}, *marker.transaction());
    CHECK(duplicate && !duplicate.value());
    const auto other_bot = processed.record_if_new({imtg::BotId{2}, 44}, *marker.transaction());
    CHECK(other_bot && other_bot.value());
    CHECK(marker.commit().outcome == imtg::CommitOutcome::Committed);

    auto intent_work = begin(backend);
    const auto intent_id = intents.append(
        imtg::RenderIntent{imtg::RenderIntentRepository::make_id(key, 5), key, 5, "screen"},
        *intent_work.transaction());
    CHECK(intent_id);
    CHECK(intent_work.commit().outcome == imtg::CommitOutcome::Committed);
    auto intent_read = begin(backend);
    const auto loaded_intent = intents.load(intent_id.value(), *intent_read.transaction());
    CHECK(loaded_intent && loaded_intent.value()->session_revision == 5
          && loaded_intent.value()->metadata == "screen");

    auto bad_intent_work = begin(backend);
    const auto bad_origin = imtg::UpdateIdentity{imtg::BotId{99}, 1};
    const auto bad_intent = intents.append(
        imtg::RenderIntent{imtg::RenderIntentRepository::make_id(key, 6, bad_origin),
                           key, 6, {}, bad_origin}, *bad_intent_work.transaction());
    CHECK(!bad_intent);
}

void test_timeout_and_unknown_recovery()
{
    imtg::SessionRepository sessions;
    imtg::ProcessedUpdateRepository processed;
    imtg::RenderIntentRepository intents;
    const auto key = conversation(8, 9);
    const auto now = imtg::ClockTime{std::chrono::milliseconds{500}};
    const auto fsm = make_fsm();

    {
        imtg::InMemoryStorageBackend backend(schema());
        imtg::AtomicUpdateProcessor processor(backend, sessions, processed, intents);
        const auto premature = processor.process({imtg::BotId{8}, 100}, key, fsm,
            imtg::FsmEvent::timeout(), now, initial_session(key, now));
        CHECK(premature.outcome == imtg::UpdateProcessOutcome::Failed);
        auto premature_check = begin(backend);
        CHECK(processed.record_if_new({imtg::BotId{8}, 100}, *premature_check.transaction()).value());
    }

    {
        imtg::InMemoryStorageBackend backend(schema());
        imtg::AtomicUpdateProcessor processor(backend, sessions, processed, intents);
        auto initial = initial_session(key, now);
        initial.deadline = now - 1s;
        // The timeout transition is intentionally explicit; a normal incoming event is converted
        // to timeout before the FSM evaluates it.
        const auto result = processor.process({imtg::BotId{8}, 1}, key, fsm,
                                               {imtg::FsmEventId{"begin", 1},
                                                imtg::FsmEventKind::Normal, {}}, now,
                                               initial);
        CHECK(result.outcome == imtg::UpdateProcessOutcome::Failed);
    }

    {
        imtg::InMemoryStorageBackend backend(schema());
        imtg::AtomicUpdateProcessor processor(backend, sessions, processed, intents);
        const auto first = processor.process({imtg::BotId{8}, 2}, key, fsm,
                                              {imtg::FsmEventId{"begin", 1},
                                               imtg::FsmEventKind::Normal, {}}, now,
                                              initial_session(key, now));
        CHECK(first.outcome == imtg::UpdateProcessOutcome::Committed);
        auto set_deadline = begin(backend);
        auto loaded = sessions.load(key, *set_deadline.transaction());
        CHECK(loaded && loaded.value());
        auto expiring = *loaded.value();
        expiring.deadline = now - 1s;
        CHECK(sessions.save(expiring, expiring.revision, *set_deadline.transaction()));
        CHECK(set_deadline.commit().outcome == imtg::CommitOutcome::Committed);
        const auto timed = processor.process({imtg::BotId{8}, 3}, key, fsm,
                                              {imtg::FsmEventId{"email", 1},
                                               imtg::FsmEventKind::Normal, "ignored"}, now + 1s,
                                              std::nullopt, imtg::UpdateProcessOptions{false});
        const imtg::FsmStateId expired{"expired", 1};
        CHECK(timed.outcome == imtg::UpdateProcessOutcome::Committed && timed.session
              && timed.session->state == expired);
    }

    {
        imtg::InMemoryStorageBackend backend(schema());
        imtg::InMemoryFailureInjection injection;
        injection.unknown_after_publish = true;
        backend.set_failure_injection(injection);
        imtg::AtomicUpdateProcessor processor(backend, sessions, processed, intents);
        const auto uncertain = processor.process({imtg::BotId{8}, 4}, key, fsm,
                                                  {imtg::FsmEventId{"begin", 1},
                                                   imtg::FsmEventKind::Normal, {}}, now,
                                                  initial_session(key, now));
        CHECK(uncertain.outcome == imtg::UpdateProcessOutcome::Unknown);
        auto recovery = begin(backend);
        CHECK(sessions.load(key, *recovery.transaction()).value().has_value());
        CHECK(processed.record_if_new({imtg::BotId{8}, 4}, *recovery.transaction()).value() == false);
    }
}

void test_atomic_processor()
{
    imtg::InMemoryStorageBackend backend(schema());
    imtg::SessionRepository sessions;
    imtg::ProcessedUpdateRepository processed;
    imtg::RenderIntentRepository intents;
    imtg::AtomicUpdateProcessor processor(backend, sessions, processed, intents);
    const auto key = conversation(3, 4);
    const auto now = imtg::ClockTime{std::chrono::milliseconds{200}};
    const auto fsm = make_fsm();

    auto first_marker = begin(backend);
    auto second_marker = begin(backend);
    const auto first_seen = processed.record_if_new({imtg::BotId{3}, 99},
                                                     *first_marker.transaction());
    const auto second_seen = processed.record_if_new({imtg::BotId{3}, 99},
                                                      *second_marker.transaction());
    CHECK(first_seen && first_seen.value() && second_seen && second_seen.value());
    CHECK(first_marker.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(second_marker.commit().outcome == imtg::CommitOutcome::RejectedConflict);

    const auto first = processor.process({imtg::BotId{3}, 1}, key, fsm,
                                         {imtg::FsmEventId{"begin", 1},
                                          imtg::FsmEventKind::Normal, {}}, now,
                                         initial_session(key, now));
    CHECK(first.outcome == imtg::UpdateProcessOutcome::Committed && first.session
          && first.session->revision == 1 && first.render_intent);
    auto intent_work = begin(backend);
    const auto stored_intent = intents.load(*first.render_intent, *intent_work.transaction());
    CHECK(stored_intent && stored_intent.value()->session_revision == 1);

    const auto duplicate = processor.process({imtg::BotId{3}, 1}, key, fsm,
                                              {imtg::FsmEventId{"begin", 1},
                                               imtg::FsmEventKind::Normal, {}}, now);
    CHECK(duplicate.outcome == imtg::UpdateProcessOutcome::Duplicate);

    const auto mismatch = processor.process({imtg::BotId{99}, 50}, key, fsm,
        {imtg::FsmEventId{"refresh", 1}, imtg::FsmEventKind::Normal, {}}, now + 1s);
    CHECK(mismatch.outcome == imtg::UpdateProcessOutcome::Failed
          && mismatch.error && mismatch.error->code == imtg::ErrorCode::InvalidArgument);
    auto mismatch_check = begin(backend);
    CHECK(processed.record_if_new({imtg::BotId{99}, 50}, *mismatch_check.transaction()).value());

    const auto invalid = processor.process({imtg::BotId{3}, 2}, key, fsm,
                                            {imtg::FsmEventId{"email", 1},
                                             imtg::FsmEventKind::Normal, {}}, now + 1s);
    CHECK(invalid.outcome == imtg::UpdateProcessOutcome::Failed);
    const auto retry = processor.process({imtg::BotId{3}, 2}, key, fsm,
                                          {imtg::FsmEventId{"email", 1},
                                           imtg::FsmEventKind::Normal, "a@b.test"}, now + 2s,
                                          std::nullopt, imtg::UpdateProcessOptions{false});
    CHECK(retry.outcome == imtg::UpdateProcessOutcome::Committed && retry.session
          && retry.session->revision == 2 && !retry.render_intent);

    const auto no_op = processor.process({imtg::BotId{3}, 3}, key, fsm,
                                          {imtg::FsmEventId{"refresh", 1},
                                           imtg::FsmEventKind::Normal, {}}, now + 3s,
                                          std::nullopt, imtg::UpdateProcessOptions{true});
    CHECK(no_op.outcome == imtg::UpdateProcessOutcome::Committed && no_op.session
          && no_op.session->revision == 2 && !no_op.render_intent);

    auto patch_options = imtg::UpdateProcessOptions{false};
    patch_options.require_expected_revision = true;
    patch_options.expected_revision = 2;
    patch_options.screen_patch = imtg::MetadataPatch<imtg::ScreenIdentity>::set({"refreshed", 1});
    const auto metadata = processor.process({imtg::BotId{3}, 6}, key, fsm,
        {imtg::FsmEventId{"refresh", 1}, imtg::FsmEventKind::Normal, {}}, now + 3s,
        std::nullopt, patch_options);
    CHECK(metadata.outcome == imtg::UpdateProcessOutcome::Committed && metadata.session
          && metadata.session->revision == 3
          && metadata.session->screen.name == "refreshed");

    const auto forced_render = processor.process({imtg::BotId{3}, 5}, key, fsm,
        {imtg::FsmEventId{"refresh", 1}, imtg::FsmEventKind::Normal, {}}, now + 3s,
        std::nullopt, imtg::UpdateProcessOptions{true, imtg::RenderPolicy::Always});
    CHECK(forced_render.outcome == imtg::UpdateProcessOutcome::Committed
          && forced_render.render_intent);
    auto forced_read = begin(backend);
    const auto forced_loaded = intents.load(*forced_render.render_intent, *forced_read.transaction());
    CHECK(forced_loaded && forced_loaded.value() && forced_loaded.value()->origin
          && forced_loaded.value()->origin->update_id == 5);

    auto verify = begin(backend);
    const auto session = sessions.load(key, *verify.transaction());
    const imtg::FsmStateId consent_state{"awaiting_consent", 1};
    CHECK(session && session.value()->state == consent_state && session.value()->revision == 3);

    imtg::InMemoryFailureInjection injection;
    injection.fail_before_publish = true;
    backend.set_failure_injection(injection);
    const auto failed = processor.process({imtg::BotId{3}, 4}, key, fsm,
                                           {imtg::FsmEventId{"consent", 1},
                                            imtg::FsmEventKind::Normal, {}}, now + 4s,
                                           std::nullopt, imtg::UpdateProcessOptions{false});
    CHECK(failed.outcome == imtg::UpdateProcessOutcome::Failed);
    auto after_failure = begin(backend);
    const auto unchanged = sessions.load(key, *after_failure.transaction());
    CHECK(unchanged && unchanged.value() && unchanged.value()->state == consent_state
          && unchanged.value()->revision == 3);
}

} // namespace

int main()
{
    test_fsm_and_encoding();
    test_repositories();
    test_atomic_processor();
    test_timeout_and_unknown_recovery();
    return 0;
}
