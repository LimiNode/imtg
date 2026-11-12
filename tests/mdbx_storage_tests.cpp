#include "imtg/storage/mdbx_storage.hpp"
#include "imtg/reconciliation/outbox.hpp"

#include "test_support.hpp"

#include <mdbx.h>

#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

imtg::StorageSchema stable_schema()
{
    return imtg::StorageSchema{1, {{"meta", 1}, {"sessions", 1},
                                   {"processed_updates", 1}, {"render_intents", 1},
                                   {"outbox", 1}, {"event_journal", 1}}};
}

std::filesystem::path test_path(const char* suffix)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::current_path() / "mdbx-test-data";
    std::filesystem::create_directories(directory);
    return directory / (std::string("imtg_mdbx_") + suffix + "_" + std::to_string(stamp));
}

std::unique_ptr<imtg::MdbxStorageBackend> open_backend(
    const std::filesystem::path& path, imtg::StorageSchema schema = stable_schema(),
    bool read_only = false)
{
    imtg::MdbxStorageConfig config;
    config.path = path.string();
    config.schema = std::move(schema);
    config.read_only = read_only;
    auto opened = imtg::MdbxStorageBackend::open(std::move(config));
    CHECK(opened);
    return std::move(opened).value();
}

imtg::UnitOfWork begin(imtg::IStorageBackend& backend,
                       imtg::TransactionMode mode = imtg::TransactionMode::ReadWrite)
{
    auto work = imtg::UnitOfWork::begin(backend, mode);
    CHECK(work);
    return std::move(work).value();
}

void test_persistence_and_read_your_writes()
{
    const auto path = test_path("persistence");
    {
        auto backend = open_backend(path);
        auto work = begin(*backend);
        const auto created = work.transaction()->create_if_absent(
            "sessions", "session-1", "pending");
        CHECK(created && created.value() == 1);
        const auto rewritten = work.transaction()->write(
            "sessions", "session-1", "ready", created.value());
        CHECK(rewritten && rewritten.value() == 1);
        const auto visible = work.transaction()->read("sessions", "session-1");
        CHECK(visible && visible.value()->value == "ready"
              && visible.value()->revision == 1);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    {
        auto backend = open_backend(path);
        auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto restored = work.transaction()->read("sessions", "session-1");
        CHECK(restored && restored.value()->value == "ready"
              && restored.value()->revision == 1);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    std::filesystem::remove(path);
}

void test_snapshot_and_atomic_records()
{
    const auto path = test_path("snapshot");
    auto backend = open_backend(path);
    {
        auto initial = begin(*backend);
        CHECK(initial.transaction()->create_if_absent("sessions", "snapshot", "old"));
        CHECK(initial.commit().outcome == imtg::CommitOutcome::Committed);
    }
    auto snapshot = begin(*backend, imtg::TransactionMode::ReadOnly);
    std::exception_ptr writer_failure;
    std::thread writer_thread([&] {
        try {
            auto writer = begin(*backend);
            CHECK(writer.transaction()->write("sessions", "snapshot", "new", 1));
            CHECK(writer.transaction()->create_if_absent(
                "processed_updates", "update-1", "seen"));
            CHECK(writer.transaction()->create_if_absent(
                "render_intents", "intent-1", "render"));
            CHECK(writer.transaction()->create_if_absent("outbox", "effect-1", "effect"));
            CHECK(writer.commit().outcome == imtg::CommitOutcome::Committed);
        } catch (...) {
            writer_failure = std::current_exception();
        }
    });
    writer_thread.join();
    if (writer_failure) std::rethrow_exception(writer_failure);
    const auto old = snapshot.transaction()->read("sessions", "snapshot");
    CHECK(old && old.value()->value == "old" && old.value()->revision == 1);
    CHECK(snapshot.commit().outcome == imtg::CommitOutcome::Committed);

    auto current = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto updated = current.transaction()->read("sessions", "snapshot");
    const auto processed = current.transaction()->read("processed_updates", "update-1");
    const auto intent = current.transaction()->read("render_intents", "intent-1");
    const auto effect = current.transaction()->read("outbox", "effect-1");
    CHECK(updated && updated.value()->value == "new" && updated.value()->revision == 2);
    CHECK(processed && intent && effect);
    CHECK(current.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_tombstones_cas_and_scans()
{
    const auto path = test_path("tombstone");
    auto backend = open_backend(path);
    auto create = begin(*backend);
    CHECK(create.transaction()->create_if_absent("sessions", "aba", "one"));
    CHECK(create.commit().outcome == imtg::CommitOutcome::Committed);

    auto erase = begin(*backend);
    CHECK(erase.transaction()->erase("sessions", "aba", 1));
    CHECK(erase.commit().outcome == imtg::CommitOutcome::Committed);

    auto recreate = begin(*backend);
    const auto generation = recreate.transaction()->create_if_absent(
        "sessions", "aba", "three");
    CHECK(generation && generation.value() == 3);
    CHECK(recreate.commit().outcome == imtg::CommitOutcome::Committed);

    auto stale = begin(*backend);
    const auto rejected = stale.transaction()->write("sessions", "aba", "stale", 1);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageConflict);
    stale.rollback();

    auto scan_writer = begin(*backend);
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-a", "a"));
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-b", "b"));
    CHECK(scan_writer.transaction()->erase("sessions", "scan-b", 1));
    CHECK(scan_writer.transaction()->create_if_absent("sessions", "scan-c", "c"));
    const auto first = scan_writer.transaction()->scan("sessions", {}, 1);
    CHECK(first && first.value().size() == 1 && first.value().front().key == "aba");
    const auto second = scan_writer.transaction()->scan(
        "sessions", first.value().front().key, 10);
    CHECK(second && second.value().size() == 2
          && second.value().front().key == "scan-a"
          && second.value().back().key == "scan-c");
    scan_writer.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_schema_gate_and_read_only()
{
    const auto migration_path = test_path("migration");
    auto migrating_schema = stable_schema();
    migrating_schema.migration_state = imtg::MigrationState::Migrating;
    auto migrating = open_backend(migration_path, migrating_schema);
    CHECK(!migrating->begin_transaction(imtg::TransactionMode::ReadOnly));
    CHECK(!migrating->validate_schema(migrating_schema));
    CHECK(migrating->close());
    std::filesystem::remove(migration_path);

    const auto path = test_path("readonly");
    auto writable = open_backend(path);
    auto work = begin(*writable);
    CHECK(work.transaction()->create_if_absent("sessions", "read-only", "value"));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(writable->close());

    auto read_only = open_backend(path, stable_schema(), true);
    CHECK(!read_only->begin_transaction(imtg::TransactionMode::ReadWrite));
    auto incompatible = stable_schema();
    incompatible.profile_version = 2;
    CHECK(!read_only->validate_schema(incompatible));
    auto read_work = begin(*read_only, imtg::TransactionMode::ReadOnly);
    const auto forbidden = read_work.transaction()->write(
        "sessions", "read-only", "changed", 1);
    CHECK(!forbidden && forbidden.error().code == imtg::ErrorCode::StorageReadOnly);
    read_work.rollback();
    CHECK(read_only->close());
    std::filesystem::remove(path);
}

void test_thread_ownership()
{
    const auto path = test_path("thread");
    auto backend = open_backend(path);
    auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
    std::atomic<bool> rejected{false};
    std::thread other([&] {
        const auto result = work.transaction()->read("sessions", "foreign");
        rejected = !result
            && result.error().code == imtg::ErrorCode::StoragePermissionDenied;
    });
    other.join();
    CHECK(rejected.load());
    work.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_namespace_isolation_and_binary_pagination()
{
    const auto path = test_path("namespaces");
    const std::array<std::string_view, 11> namespaces = {
        "meta", "sessions", "processed_updates", "render_intents",
        "render_intent_claims", "render_intent_generations", "outbox_effects",
        "outbox_dispatch_targets", "applied_targets", "event_journal", "outbox"};
    const std::vector<std::string> keys = {
        std::string("\0", 1), std::string("a\0", 2), std::string("a\0b", 3),
        std::string("\x80", 1), std::string("\xff", 1)};
    {
        auto backend = open_backend(path);
        auto work = begin(*backend);
        for (const auto name_space : namespaces) {
            for (const auto& key : keys) {
                CHECK(work.transaction()->create_if_absent(name_space, key, name_space));
                CHECK(work.transaction()->watch(name_space, key, 1));
            }
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        auto mutations = begin(*backend);
        for (const auto name_space : namespaces) {
            CHECK(mutations.transaction()->write(name_space, keys[0], "changed", 1));
            CHECK(mutations.transaction()->watch(name_space, keys[0], 2));
            CHECK(mutations.transaction()->erase(name_space, keys[1], 1));
        }
        CHECK(mutations.commit().outcome == imtg::CommitOutcome::Committed);
        auto recreate = begin(*backend);
        for (const auto name_space : namespaces) {
            const auto created = recreate.transaction()->create_if_absent(name_space, keys[1], name_space);
            CHECK(created && created.value() == 3);
            CHECK(!recreate.transaction()->write(name_space, keys[0], "stale", 1));
            CHECK(!recreate.transaction()->watch(name_space, keys[0], 1));
        }
        CHECK(recreate.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
    }
    auto backend = open_backend(path);
    auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
    for (const auto name_space : namespaces) {
        std::string continuation;
        for (std::size_t index = 0; index != keys.size(); ++index) {
            const auto page = work.transaction()->scan(name_space, continuation, 1);
            CHECK(page && page.value().size() == 1);
            const auto& row = page.value().front();
            CHECK(row.key == keys[index]);
            CHECK(row.record.value == (index == 0 ? "changed" : name_space));
            CHECK(row.record.revision == (index == 0 ? 2 : index == 1 ? 3 : 1));
            continuation = row.key;
        }
        const auto end = work.transaction()->scan(name_space, continuation, 1);
        CHECK(end && end.value().empty());
        const auto absent_cursor = work.transaction()->scan(name_space, "a", 1);
        CHECK(absent_cursor && absent_cursor.value().front().key == keys[1]);
    }
    CHECK(!work.transaction()->read("unregistered", "key"));
    CHECK(!work.transaction()->scan("unregistered", {}, 1));
    CHECK(!work.transaction()->watch("unregistered", "key", 0));
    CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    auto invalid = begin(*backend);
    CHECK(!invalid.transaction()->create_if_absent("unregistered", "key", "value"));
    CHECK(!invalid.transaction()->write("unregistered", "key", "value", 1));
    CHECK(!invalid.transaction()->erase("unregistered", "key", 1));
    const std::string oversized_key(65536, 'x');
    const auto oversized = invalid.transaction()->read("sessions", oversized_key);
    CHECK(!oversized && oversized.error().code == imtg::ErrorCode::StorageRecordTooLarge);
    CHECK(!invalid.transaction()->create_if_absent("sessions", oversized_key, "value"));
    CHECK(!invalid.transaction()->watch("sessions", oversized_key, 0));
    CHECK(!invalid.transaction()->scan("sessions", oversized_key, 1));
    invalid.rollback();
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_public_meta_and_sparse_scans()
{
    const auto path = test_path("meta-sparse");
    auto backend = open_backend(path);
    {
        auto work = begin(*backend);
        const auto empty = work.transaction()->scan("meta", {}, 1);
        CHECK(empty && empty.value().empty());
        CHECK(work.transaction()->create_if_absent("meta", "imtg/schema/v1", "public"));
        for (int index = 0; index != 256; ++index) {
            CHECK(work.transaction()->create_if_absent("meta", "dead-" + std::to_string(index), "deleted"));
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    {
        auto work = begin(*backend);
        for (int index = 0; index != 256; ++index) {
            CHECK(work.transaction()->erase("meta", "dead-" + std::to_string(index), 1));
        }
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    for (int pass = 0; pass != 2; ++pass) {
        auto work = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto page = work.transaction()->scan("meta", {}, 1);
        CHECK(page && page.value().size() == 1);
        CHECK(page.value().front().key == "imtg/schema/v1"
              && page.value().front().record.value == "public");
        const auto schema = backend->schema();
        CHECK(schema && schema.value() == stable_schema());
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        CHECK(backend->close());
        if (pass == 0) backend = open_backend(path);
    }
    std::filesystem::remove(path);
}

void test_close_with_live_transactions()
{
    const auto path = test_path("close");
    auto backend = open_backend(path);
    auto work = begin(*backend);
    CHECK(!backend->close());
    CHECK(backend->is_open());
    CHECK(work.transaction()->create_if_absent("sessions", "live", "value"));
    work.rollback();
    CHECK(backend->close());
    backend = open_backend(path);
    auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
    const auto absent = read.transaction()->read("sessions", "live");
    CHECK(absent && !absent.value());
    CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    CHECK(backend->close());
    std::filesystem::remove(path);
}

void test_unprefixed_draft_is_rejected()
{
    const auto path = test_path("legacy");
    MDBX_env* environment = nullptr;
    CHECK(mdbx_env_create(&environment) == MDBX_SUCCESS);
    CHECK(mdbx_env_set_maxdbs(environment, 8) == MDBX_SUCCESS);
    CHECK(mdbx_env_open(environment, path.string().c_str(), MDBX_NOSUBDIR, 0664) == MDBX_SUCCESS);
    MDBX_txn* native = nullptr;
    CHECK(mdbx_txn_begin(environment, nullptr, MDBX_TXN_READWRITE, &native) == MDBX_SUCCESS);
    MDBX_dbi meta{};
    CHECK(mdbx_dbi_open(native, "meta", MDBX_CREATE, &meta) == MDBX_SUCCESS);
    std::string key = "imtg/schema/v1";
    std::string value = "unreleased unprefixed schema";
    MDBX_val physical_key{key.data(), key.size()};
    MDBX_val physical_value{value.data(), value.size()};
    CHECK(mdbx_put(native, meta, &physical_key, &physical_value, MDBX_UPSERT) == MDBX_SUCCESS);
    CHECK(mdbx_txn_commit(native) == MDBX_SUCCESS);
    CHECK(mdbx_env_close(environment) == MDBX_SUCCESS);
    imtg::MdbxStorageConfig config;
    config.path = path.string();
    const auto rejected = imtg::MdbxStorageBackend::open(config);
    CHECK(!rejected && rejected.error().code == imtg::ErrorCode::StorageSchemaFailure);
    std::filesystem::remove(path);
}

void test_repositories_tx1_tx2_delivery_and_reopen()
{
    const auto path = test_path("repositories");
    auto backend = open_backend(path);
    const imtg::ConversationKey conversation{imtg::BotId{7}, imtg::ChatId{42}, imtg::Chat{}};
    const imtg::UpdateIdentity update{conversation.bot_id, 19};
    const imtg::ClockTime now{std::chrono::seconds(100)};
    imtg::SessionRepository sessions;
    imtg::ProcessedUpdateRepository processed;
    imtg::RenderIntentRepository intents;
    imtg::RenderIntentClaimRepository claims;
    imtg::DesiredGenerationRepository generations;
    imtg::OutboxRepository outbox;
    imtg::AppliedTargetRepository applied;
    imtg::Fsm fsm;
    CHECK(fsm.add_transition({"start", 1}, {"begin", 1}, {"ready", 1}));
    imtg::SessionRecord initial;
    initial.conversation = conversation;
    initial.state = {"start", 1};
    initial.screen = {"start", 1};
    initial.created_at = now;
    initial.last_transition_at = now;
    {
        imtg::AtomicUpdateProcessor processor(*backend, sessions, processed, intents);
        const auto failed = processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"missing", 1}}, now, initial);
        CHECK(failed.outcome == imtg::UpdateProcessOutcome::Failed);
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto absent_session = sessions.load(conversation, *read.transaction());
        const auto absent_marker = read.transaction()->read("processed_updates", update.encode());
        const auto absent_intents = read.transaction()->scan("render_intents", {}, 10);
        CHECK(absent_session && !absent_session.value());
        CHECK(absent_marker && !absent_marker.value());
        CHECK(absent_intents && absent_intents.value().empty());
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    imtg::UpdateProcessResult tx1;
    {
        imtg::AtomicUpdateProcessor processor(*backend, sessions, processed, intents);
        tx1 = processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"begin", 1}}, now, initial);
        CHECK(tx1.outcome == imtg::UpdateProcessOutcome::Committed);
        CHECK(tx1.session && tx1.render_intent);
        CHECK(processor.process(update, conversation, fsm,
            imtg::FsmEvent{{"begin", 1}}, now).outcome == imtg::UpdateProcessOutcome::Duplicate);
    }
    imtg::RenderIntentClaim intent_claim;
    imtg::DesiredGeneration generation;
    {
        auto work = begin(*backend);
        const auto marker = generations.ensure(conversation.encode(), *work.transaction());
        const auto claimed = claims.claim_render_intents(
            1, now, std::chrono::hours(1), {}, *work.transaction());
        CHECK(marker && claimed && claimed.value().claims.size() == 1);
        generation = marker.value();
        intent_claim = claimed.value().claims.front();
        CHECK(intent_claim.id == *tx1.render_intent);
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
    }
    const auto target = imtg::make_logical_target_identity(conversation, "body");
    CHECK(target);
    imtg::ReconciliationInput input{*tx1.render_intent, target.value(), imtg::EffectKind::Edit,
        "body", tx1.session->revision, 0, imtg::RenderPolicy::OnChange, 1, "ready payload"};
    input.conversation = conversation;
    input.session_revision = tx1.session->revision;
    input.desired_generation = generation.value;
    input.desired_generation_row_revision = generation.row_revision;
    input.context = imtg::ReconciliationContext{
        *tx1.render_intent, conversation, tx1.session->revision, tx1.session->revision,
        generation.value, generation.row_revision, target.value(), imtg::EffectKind::Edit,
        "body", 0, 0, imtg::RenderPolicy::OnChange,
        intent_claim.claim_token, intent_claim.row_revision};
    const auto plan = imtg::ReconciliationPlanner{}.build(input);
    CHECK(plan && !plan.value().no_op);
    {
        imtg::AtomicReconciliation publisher(*backend, claims, outbox);
        auto work = begin(*backend);
        CHECK(publisher.stage(plan.value(), *tx1.render_intent, intent_claim.claim_token,
                              intent_claim.row_revision, now, *work.transaction()));
        work.rollback();
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto absent = outbox.load(plan.value().effect.id, *read.transaction());
        const auto unchanged = claims.inspect(*tx1.render_intent, now, *read.transaction());
        CHECK(absent && !absent.value());
        CHECK(unchanged && unchanged.value()
              && unchanged.value()->state == imtg::RenderIntentLifecycleState::Claimed
              && unchanged.value()->row_revision == intent_claim.row_revision);
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
        const auto published = publisher.publish(plan.value(), *tx1.render_intent,
            intent_claim.claim_token, intent_claim.row_revision, now);
        CHECK(published.outcome == imtg::ReconciliationOutcome::Planned && published.effect);
    }
    imtg::OutboxEffect admitted;
    {
        auto work = begin(*backend);
        const auto claimed = outbox.claim_outbox_effects(
            1, now, std::chrono::hours(1), {}, *work.transaction());
        CHECK(claimed && claimed.value().effects.size() == 1);
        const auto claim = claimed.value().effects.front();
        CHECK(work.commit().outcome == imtg::CommitOutcome::Committed);
        auto admission = begin(*backend);
        const auto result = outbox.mark_dispatch_admitted(claim.id, claim.claim_token,
            claim.revision, now, *admission.transaction());
        CHECK(result);
        admitted = result.value();
        CHECK(admission.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    backend = open_backend(path);
    {
        auto read = begin(*backend);
        const auto session = sessions.load(conversation, *read.transaction());
        const auto intent = intents.load(*tx1.render_intent, *read.transaction());
        const auto claim = claims.inspect(*tx1.render_intent, now, *read.transaction());
        const auto marker = generations.load(conversation.encode(), *read.transaction());
        const auto effect = outbox.load(admitted.id, *read.transaction());
        const auto owner = read.transaction()->read("outbox_dispatch_targets", target.value());
        CHECK(session && session.value() && session.value()->state.name == "ready"
              && session.value()->revision == tx1.session->revision);
        CHECK(intent && intent.value() && intent.value()->id == *tx1.render_intent);
        CHECK(claim && claim.value() && claim.value()->state == imtg::RenderIntentLifecycleState::Planned
              && claim.value()->claim_token == intent_claim.claim_token);
        CHECK(marker && marker.value().value == generation.value
              && marker.value().row_revision == generation.row_revision);
        CHECK(effect && effect.value() && effect.value()->state == imtg::OutboxEffectState::DispatchAdmitted
              && effect.value()->payload == "ready payload"
              && effect.value()->revision == admitted.revision);
        CHECK(owner && owner.value() && owner.value()->value.substr(0, 3) == "DT1");
        CHECK(owner.value()->value.find(admitted.id.value) != std::string::npos);
        const auto duplicate = processed.record_if_new(update, *read.transaction());
        CHECK(duplicate && !duplicate.value());
        const auto acknowledged = outbox.acknowledge_success(admitted.id, admitted.claim_token,
            admitted.revision, now, "telegram-message-42", applied, *read.transaction());
        CHECK(acknowledged && acknowledged.value().state == imtg::OutboxEffectState::Succeeded);
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    backend = open_backend(path);
    {
        auto read = begin(*backend, imtg::TransactionMode::ReadOnly);
        const auto effect = outbox.load(admitted.id, *read.transaction());
        const auto confirmed = applied.load(target.value(), *read.transaction());
        const auto owner = read.transaction()->read("outbox_dispatch_targets", target.value());
        CHECK(effect && effect.value() && effect.value()->state == imtg::OutboxEffectState::Succeeded);
        CHECK(confirmed && confirmed.value()
              && confirmed.value()->confirmed_applied_revision == tx1.session->revision
              && confirmed.value()->physical_identity == "telegram-message-42"
              && confirmed.value()->last_confirmed_effect == admitted.id);
        CHECK(owner && owner.value() && owner.value()->revision == 2);
        CHECK(owner.value()->value == std::string("DT1") + std::string(16, '\0'));
        CHECK(read.commit().outcome == imtg::CommitOutcome::Committed);
    }
    CHECK(backend->close());
    std::filesystem::remove(path);
}

} // namespace

int main()
{
    try {
        test_persistence_and_read_your_writes();
        test_snapshot_and_atomic_records();
        test_tombstones_cas_and_scans();
        test_schema_gate_and_read_only();
        test_thread_ownership();
        test_namespace_isolation_and_binary_pagination();
        test_public_meta_and_sparse_scans();
        test_close_with_live_transactions();
        test_unprefixed_draft_is_rejected();
        test_repositories_tx1_tx2_delivery_and_reopen();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
