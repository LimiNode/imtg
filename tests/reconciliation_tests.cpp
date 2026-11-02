#include "imtg/reconciliation/outbox.hpp"
#include "imtg/sessions/session.hpp"

#include <cassert>

using namespace imtg;

int main() {
    LogicalEffectSlot slot{RenderIntentId{"intent-1"}, "target-1", EffectKind::Edit, "body"};
    ReconciliationPlanner planner;
    ReconciliationInput input{RenderIntentId{"intent-1"}, "target-1", EffectKind::Edit, "body", 2, 1,
                              RenderPolicy::OnChange, 1, "hello"};
    auto plan = planner.build(input);
    assert(plan && !plan.value().no_op);
    auto encoded = encode_outbox_effect(plan.value().effect);
    assert(encoded);
    auto decoded = decode_outbox_effect(encoded.value());
    assert(decoded && decoded.value().payload == "hello");
    InMemoryStorageBackend backend;
    OutboxRepository repository;
    auto unit = UnitOfWork::begin(backend, TransactionMode::ReadWrite);
    assert(unit);
    auto inserted = repository.create_or_validate(plan.value().effect, *unit.value().transaction());
    assert(inserted);
    auto replay = repository.create_or_validate(plan.value().effect, *unit.value().transaction());
    assert(replay && replay.value().id == inserted.value().id);
    auto conflict = plan.value().effect;
    conflict.payload = "different";
    // Keep the original digest to demonstrate validation of a mismatched payload.
    auto rejected = repository.create_or_validate(conflict, *unit.value().transaction());
    assert(!rejected && rejected.error().code == ErrorCode::PayloadVersionMismatch);
    assert(unit.value().commit().outcome == CommitOutcome::Committed);
    AppliedTargetRepository applied;
    auto target_unit = UnitOfWork::begin(backend, TransactionMode::ReadWrite);
    assert(target_unit);
    AppliedTargetState target;
    target.target_identity = "target-1";
    target.desired_revision = 2;
    auto saved = applied.save(target, 0, *target_unit.value().transaction());
    assert(saved);
    assert(target_unit.value().commit().outcome == CommitOutcome::Committed);
    auto read_unit = UnitOfWork::begin(backend, TransactionMode::ReadOnly);
    assert(read_unit);
    auto loaded = applied.load("target-1", *read_unit.value().transaction());
    assert(loaded && loaded.value() && loaded.value()->desired_revision == 2);
    auto noop = planner.build(ReconciliationInput{RenderIntentId{"intent-1"}, "target-1", EffectKind::Edit,
                                                    "body", 1, 1, RenderPolicy::OnChange, 1, "hello"});
    assert(noop && noop.value().no_op);
    return 0;
}
