#include "imtg/reconciliation/outbox.hpp"
#include "imtg/storage/time_encoding.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace imtg {
namespace {
constexpr std::string_view outbox_namespace = "outbox_effects";
constexpr std::string_view applied_namespace = "applied_targets";
constexpr std::size_t max_field_size = 1024 * 1024;
constexpr std::size_t max_record_size = 4 * 1024 * 1024;
constexpr std::size_t max_logical_slot_size = 4096;

Error err(ErrorCode code, const char* message) { return Error{code, message}; }

void put_u32(std::string& s, std::uint32_t v) {
    for (unsigned i = 0; i != 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void put_u64(std::string& s, std::uint64_t v) {
    for (unsigned i = 0; i != 8; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
void put_string(std::string& s, std::string_view v) { put_u64(s, v.size()); s.append(v); }

class Reader {
public:
    explicit Reader(std::string_view s) : m_s(s) {}
    Result<std::uint32_t> u32() {
        if (m_s.size() - m_p < 4) return err(ErrorCode::TruncatedEncoding, "effect encoding is truncated");
        std::uint32_t v = 0; for (unsigned i=0;i<4;++i) v |= std::uint32_t(static_cast<unsigned char>(m_s[m_p++])) << (8*i); return v;
    }
    Result<std::uint64_t> u64() {
        if (m_s.size() - m_p < 8) return err(ErrorCode::TruncatedEncoding, "effect encoding is truncated");
        std::uint64_t v = 0; for (unsigned i=0;i<8;++i) v |= std::uint64_t(static_cast<unsigned char>(m_s[m_p++])) << (8*i); return v;
    }
    Result<std::int64_t> i64() {
        const auto raw = u64();
        if (!raw) return raw.error();
        std::int64_t value{};
        const auto bits = raw.value();
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    Result<std::string> str() {
        auto n=u64(); if(!n) return n.error(); if(n.value()>m_s.size()-m_p || n.value()>max_field_size) return err(ErrorCode::StorageRecordTooLarge,"effect field is too large");
        std::string v(m_s.substr(m_p, static_cast<std::size_t>(n.value()))); m_p += static_cast<std::size_t>(n.value()); return v;
    }
    Result<unsigned char> byte() {
        if (m_s.size() - m_p < 1) return err(ErrorCode::TruncatedEncoding, "effect encoding is truncated");
        return static_cast<unsigned char>(m_s[m_p++]);
    }
    std::size_t remaining() const noexcept { return m_s.size()-m_p; }
private: std::string_view m_s; std::size_t m_p{};
};

std::string digest(std::string_view value) {
    constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
    std::uint64_t h = offset_basis;
    for (unsigned char c : value) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream out; out << "fnv1a64:" << std::hex << std::setw(16) << std::setfill('0') << h; return out.str();
}

std::string hex_encode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char c : value) {
        result.push_back(digits[c >> 4]);
        result.push_back(digits[c & 0x0f]);
    }
    return result;
}

std::string target_key(std::string_view value) { return std::string(value); }

Result<ClockTime> millis_time(std::uint64_t raw) {
    return decode_storage_timestamp(raw);
}

Result<std::int64_t> time_millis(ClockTime value) {
    return encode_storage_timestamp(value);
}

void put_i64(std::string& s, std::int64_t value) {
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    put_u64(s, bits);
}

Result<void> validate_effect(const OutboxEffect& e) {
    if (e.id.value.empty() || e.slot.render_intent_id.value.empty() || e.slot.target_identity.empty()
        || e.slot.effect_position.empty() || e.payload_version == 0)
        return err(ErrorCode::InvalidArgument, "effect identity or payload is invalid");
    if (e.slot.render_intent_id.value.size() > max_logical_slot_size
        || e.slot.target_identity.size() > max_logical_slot_size
        || e.slot.effect_position.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical effect field is too large");
    if (e.id.value != OutboxEffectId::from_slot(e.slot).value)
        return err(ErrorCode::InvalidArgument, "effect id does not match logical slot");
    if (static_cast<unsigned>(e.slot.effect_kind) > static_cast<unsigned>(EffectKind::Replace)
        || static_cast<unsigned>(e.state) > static_cast<unsigned>(OutboxEffectState::Superseded))
        return err(ErrorCode::InvalidArgument, "effect enum value is invalid");
    if (e.slot.encode().size() > max_logical_slot_size || e.id.value.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical effect identity is too large");
    if (e.payload.size() > max_field_size || e.payload_digest.size() > max_field_size)
        return err(ErrorCode::StorageRecordTooLarge, "effect payload is too large");
    if (e.payload_digest != digest(e.payload)) return err(ErrorCode::PayloadVersionMismatch, "payload digest mismatch");
    return {};
}

std::string encode_slot(const LogicalEffectSlot& slot) {
    std::string s; s.append("LES1",4); put_string(s,slot.render_intent_id.value); put_string(s,slot.target_identity);
    s.push_back(static_cast<char>(slot.effect_kind)); put_string(s,slot.effect_position); return s;
}

} // namespace

std::string LogicalEffectSlot::encode() const { return encode_slot(*this); }

OutboxEffectId OutboxEffectId::from_slot(const LogicalEffectSlot& slot) {
    const auto bytes = slot.encode();
    // Encode the complete canonical tuple rather than a digest: payload is not
    // part of identity, and a hash-only identifier would make collisions
    // indistinguishable logical slots.
    return OutboxEffectId{std::string("OE1:") + hex_encode(bytes)};
}

Result<StorageBytes> encode_outbox_effect(const OutboxEffect& e) {
    const auto valid = validate_effect(e); if (!valid) return valid.error();
    std::string s{"OE1"};
    put_string(s,e.id.value); put_string(s,e.slot.render_intent_id.value); put_string(s,e.slot.target_identity);
    s.push_back(static_cast<char>(e.slot.effect_kind)); put_string(s,e.slot.effect_position);
    put_u32(s,e.payload_version); put_string(s,e.payload); put_string(s,e.payload_digest); put_u64(s,e.desired_revision);
    s.push_back(static_cast<char>(e.state)); put_u64(s,e.claim_token.value);
    const auto lease = time_millis(e.lease_until);
    const auto next = time_millis(e.next_eligible_at);
    if (!lease || !next) return lease ? next.error() : lease.error();
    put_i64(s, lease.value());
    put_i64(s, next.value());
    put_u32(s,e.attempt_count);
    if (s.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge, "effect record is too large");
    return s;
}

Result<OutboxEffect> decode_outbox_effect(std::string_view bytes) {
    if (bytes.size()<3 || bytes.substr(0,3)!="OE1") return err(ErrorCode::UnsupportedVersion,"effect encoding version is unsupported");
    if (bytes.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge,"effect record is too large");
    Reader r(bytes.substr(3)); OutboxEffect e;
    auto id=r.str(); auto ri=r.str(); auto target=r.str(); auto kind=r.byte(); auto pos=r.str(); auto pv=r.u32(); auto payload=r.str(); auto dg=r.str(); auto desired=r.u64();
    if(!id||!ri||!target||!kind||!pos||!pv||!payload||!dg||!desired||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.id={id.value()}; e.slot={RenderIntentId{ri.value()},target.value(),static_cast<EffectKind>(kind.value()),pos.value()}; e.payload_version=pv.value(); e.payload=payload.value(); e.payload_digest=dg.value(); e.desired_revision=desired.value();
    auto st=r.byte();
    if (!st) return st.error();
    e.state=static_cast<OutboxEffectState>(st.value()); auto token=r.u64(); auto lease=r.i64(); auto next=r.i64(); auto attempts=r.u32();
    if(!token||!lease||!next||!attempts||r.remaining()!=0) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.claim_token={token.value()};
    std::uint64_t lease_bits{}, next_bits{};
    std::memcpy(&lease_bits, &lease.value(), sizeof(lease_bits));
    std::memcpy(&next_bits, &next.value(), sizeof(next_bits));
    auto lt=millis_time(lease_bits); auto nt=millis_time(next_bits); if(!lt||!nt) return err(ErrorCode::InvalidEncoding,"effect timestamp is invalid"); e.lease_until=lt.value(); e.next_eligible_at=nt.value(); e.attempt_count=attempts.value();
    if (e.slot.encode().size() > max_logical_slot_size || e.id.value.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical effect identity is too large");
    const auto valid=validate_effect(e); if(!valid) return valid.error(); return e;
}

Result<std::optional<OutboxEffect>> OutboxRepository::load(const OutboxEffectId& id, ITransaction& tx) const {
    if(id.value.empty()) return err(ErrorCode::InvalidArgument,"effect id is empty");
    if (id.value.size() > max_logical_slot_size) return err(ErrorCode::StorageRecordTooLarge,"effect id is too large");
    auto row=tx.read(outbox_namespace,id.value); if(!row) return row.error(); if(!row.value()) return std::optional<OutboxEffect>{};
    auto e=decode_outbox_effect(row.value()->value); if(!e) return e.error();
    if (e.value().id == id && e.value().id.value == id.value) {
        e.value().revision=row.value()->revision; return std::optional<OutboxEffect>{e.value()};
    }
    return err(ErrorCode::StorageSerializationFailure, "effect identity does not match storage key");
}

Result<OutboxEffect> OutboxRepository::create_or_validate(const OutboxEffect& effect, ITransaction& tx) const {
    const auto valid=validate_effect(effect); if(!valid) return valid.error();
    if (effect.state != OutboxEffectState::Pending || effect.claim_token.value != 0
        || effect.attempt_count != 0)
        return err(ErrorCode::InvalidState, "new outbox effect is not in its initial state");
    auto existing=load(effect.id,tx); if(!existing) return existing.error();
    if(existing.value()) {
        if(existing.value()->slot==effect.slot && existing.value()->payload_version==effect.payload_version
           && existing.value()->payload==effect.payload && existing.value()->payload_digest==effect.payload_digest
           && existing.value()->desired_revision==effect.desired_revision)
            return *existing.value();
        return err(ErrorCode::StorageConflict,"logical effect slot already contains a different payload");
    }
    auto encoded=encode_outbox_effect(effect); if(!encoded) return encoded.error(); auto rev=tx.create_if_absent(outbox_namespace,effect.id.value,encoded.value()); if(!rev) return rev.error(); auto result=effect; result.revision=rev.value(); return result;
}

namespace {
constexpr std::string_view applied_magic = "AT1";
Result<StorageBytes> encode_applied(const AppliedTargetState& s) {
    if(s.target_identity.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty");
    if (s.target_identity.size() > max_field_size || (s.physical_identity && s.physical_identity->size() > max_field_size)
        || (s.last_confirmed_effect && s.last_confirmed_effect->value.size() > max_logical_slot_size))
        return err(ErrorCode::StorageRecordTooLarge, "applied target record is too large");
    if (s.confirmed_applied_revision > s.desired_revision)
        return err(ErrorCode::InvalidArgument, "confirmed revision exceeds desired revision");
    std::string b{applied_magic}; put_string(b,s.target_identity); b.push_back(s.physical_identity?'\x01':'\x00'); if(s.physical_identity) put_string(b,*s.physical_identity); put_u64(b,s.desired_revision); put_u64(b,s.confirmed_applied_revision); b.push_back(s.last_confirmed_effect?'\x01':'\x00'); if(s.last_confirmed_effect) put_string(b,s.last_confirmed_effect->value);
    if (b.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge, "applied target record is too large");
    return b;
}
Result<AppliedTargetState> decode_applied(std::string_view b) {
    if(b.substr(0,3)!=applied_magic) return err(ErrorCode::UnsupportedVersion,"applied target version is unsupported");
    if (b.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge,"applied target record is too large");
    Reader r(b.substr(3)); AppliedTargetState s;
    auto t=r.str(); if(!t||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"applied target encoding is malformed");
    s.target_identity=t.value();
    if (s.target_identity.empty()) return err(ErrorCode::InvalidEncoding,"applied target identity is empty");
    auto ph=r.byte(); if(!ph) return ph.error(); if(ph.value()>1) return err(ErrorCode::InvalidEncoding,"applied target marker is malformed");
    if(ph.value()) { auto v=r.str(); if(!v) return v.error(); s.physical_identity=v.value(); }
    auto d=r.u64(); auto a=r.u64(); if(!d||!a||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"applied target encoding is malformed");
    s.desired_revision=d.value(); s.confirmed_applied_revision=a.value();
    if (s.confirmed_applied_revision > s.desired_revision)
        return err(ErrorCode::InvalidEncoding,"confirmed revision exceeds desired revision");
    auto has=r.byte(); if(!has) return has.error(); if(has.value()>1) return err(ErrorCode::InvalidEncoding,"applied effect marker is malformed");
    if(has.value()) { auto x=r.str(); if(!x) return x.error(); if (x.value().empty()) return err(ErrorCode::InvalidEncoding,"applied effect identity is empty"); s.last_confirmed_effect=OutboxEffectId{x.value()}; }
    if(r.remaining()!=0) return err(ErrorCode::InvalidEncoding,"applied target has trailing bytes"); return s;
}
}

Result<std::optional<AppliedTargetState>> AppliedTargetRepository::load(std::string_view id, ITransaction& tx) const { if(id.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty"); if (id.size() > max_logical_slot_size) return err(ErrorCode::StorageRecordTooLarge,"target identity is too large"); auto row=tx.read(applied_namespace,target_key(id)); if(!row) return row.error(); if(!row.value()) return std::optional<AppliedTargetState>{}; auto s=decode_applied(row.value()->value); if(!s) return s.error(); if(s.value().target_identity != id) return err(ErrorCode::StorageSerializationFailure,"target identity does not match storage key"); s.value().revision=row.value()->revision; return std::optional<AppliedTargetState>{s.value()}; }
Result<AppliedTargetState> AppliedTargetRepository::save(const AppliedTargetState& state, StorageRevision expected, ITransaction& tx) const {
    const auto current = load(state.target_identity, tx);
    if (!current) return current.error();
    if (expected == 0) {
        if (current.value()) return err(ErrorCode::StorageConflict, "applied target already exists");
    } else {
        if (!current.value() || current.value()->revision != expected)
            return err(ErrorCode::StorageConflict, "applied target revision is stale");
        if (state.confirmed_applied_revision < current.value()->confirmed_applied_revision)
            return err(ErrorCode::StorageConflict, "confirmed revision cannot decrease");
    }
    const auto enc=encode_applied(state); if(!enc) return enc.error();
    Result<StorageRevision> rev = expected==0 ? tx.create_if_absent(applied_namespace,state.target_identity,enc.value()) : tx.write(applied_namespace,state.target_identity,enc.value(),expected);
    if(!rev) return rev.error(); auto out=state; out.revision=rev.value(); return out;
}

Result<ReconciliationPlan> ReconciliationPlanner::build(const ReconciliationInput& in) const {
    if(in.render_intent_id.value.empty()||in.target_identity.empty()||in.effect_position.empty()||in.payload_version==0) return err(ErrorCode::InvalidArgument,"reconciliation input is incomplete");
    if (in.render_intent_id.value.size() > max_logical_slot_size || in.target_identity.size() > max_logical_slot_size
        || in.effect_position.size() > max_logical_slot_size || in.canonical_payload.size() > max_field_size)
        return err(ErrorCode::StorageRecordTooLarge, "reconciliation input is too large");
    if (in.session_revision == 0 && in.conversation)
        return err(ErrorCode::InvalidArgument, "session revision is required with a conversation");
    if (in.desired_generation != 0 && in.desired_generation_row_revision == 0)
        return err(ErrorCode::InvalidArgument, "desired generation row revision is required");
    LogicalEffectSlot slot{in.render_intent_id,in.target_identity,in.effect_kind,in.effect_position};
    ReconciliationPlan plan;
    plan.render_intent_id = in.render_intent_id;
    plan.target_identity = in.target_identity;
    plan.effect_kind = in.effect_kind;
    plan.effect_position = in.effect_position;
    plan.desired_revision = in.desired_revision;
    plan.applied_revision = in.applied_revision;
    plan.render_policy = in.render_policy;
    plan.conversation = in.conversation;
    plan.session_revision = in.session_revision;
    plan.desired_generation = in.desired_generation;
    plan.desired_generation_row_revision = in.desired_generation_row_revision;
    plan.applied_target_revision = in.applied_target_revision;
    if(in.render_policy!=RenderPolicy::Always && in.desired_revision<=in.applied_revision) {
        plan.no_op = true;
        return plan;
    }
    OutboxEffect e; e.slot=slot; e.id=OutboxEffectId::from_slot(slot); e.payload_version=in.payload_version; e.payload=in.canonical_payload; e.payload_digest=digest(e.payload); e.desired_revision=in.desired_revision; plan.effect = std::move(e); return plan;
}

Result<std::optional<OutboxEffect>> AtomicReconciliation::stage(
    const ReconciliationPlan& plan, const RenderIntentId& id, ClaimToken token,
    StorageRevision row_revision, ClockTime now, ITransaction& tx) const {
    const auto planned_id = plan.render_intent_id.value.empty()
        ? (plan.effect.slot.render_intent_id.value.empty() ? id : plan.effect.slot.render_intent_id)
        : plan.render_intent_id;
    if (!(planned_id == id)) return err(ErrorCode::StorageConflict,"plan belongs to another render intent");
    if (plan.no_op && (plan.target_identity.empty() || plan.desired_revision == 0)) {
        return err(ErrorCode::StorageConflict,"no-op plan has no applied-state context");
    }
    if (!plan.no_op && (!(plan.effect.slot.render_intent_id == id)
        || plan.effect.slot.target_identity != plan.target_identity
        || plan.effect.slot.effect_kind != plan.effect_kind
        || plan.effect.slot.effect_position != plan.effect_position
        || plan.effect.desired_revision != plan.desired_revision)) {
        return err(ErrorCode::StorageConflict,"plan identity or revision is inconsistent");
    }
    RenderIntentRepository intents;
    const auto intent = intents.load(id, tx);
    if (!intent || !intent.value()) return intent ? Result<std::optional<OutboxEffect>>(err(ErrorCode::StorageNotFound,"render intent is absent")) : Result<std::optional<OutboxEffect>>(intent.error());
    if (plan.conversation && intent.value()->conversation != *plan.conversation) return err(ErrorCode::StorageConflict,"render intent conversation is stale");
    if (plan.session_revision != 0 && intent.value()->session_revision != plan.session_revision) return err(ErrorCode::StorageConflict,"render intent session revision is stale");
    if (plan.desired_revision != 0 && intent.value()->session_revision != plan.desired_revision)
        return err(ErrorCode::StorageConflict, "plan desired revision is stale");
    if (plan.conversation && plan.session_revision != 0) {
        const auto watched = tx.watch("sessions", plan.conversation->encode(), plan.session_revision);
        if (!watched) return watched.error();
    }
    if (plan.conversation && plan.desired_generation != 0) {
        DesiredGenerationRepository generations;
        const auto marker = generations.load(plan.conversation->encode(), tx);
        if (!marker || marker.value().value != plan.desired_generation
            || marker.value().row_revision != plan.desired_generation_row_revision) {
            return marker ? Result<std::optional<OutboxEffect>>(err(ErrorCode::StorageConflict,"desired generation is stale")) : Result<std::optional<OutboxEffect>>(marker.error());
        }
        const auto watched = tx.watch("render_intent_generations", plan.conversation->encode(), plan.desired_generation_row_revision);
        if (!watched) return watched.error();
    }
    if (!plan.target_identity.empty()) {
        const auto watched = tx.watch("applied_targets", plan.target_identity, plan.applied_target_revision);
        if (!watched) return watched.error();
        if (plan.no_op) {
            AppliedTargetRepository applied;
            const auto state = applied.load(plan.target_identity, tx);
            if (!state || !state.value()
                || state.value()->confirmed_applied_revision < plan.desired_revision
                || plan.render_policy == RenderPolicy::Always) {
                return err(ErrorCode::StorageConflict,"no-op plan lacks exact applied-state proof");
            }
        }
    }
    Result<OutboxEffect> staged = plan.no_op ? OutboxEffect{} : m_outbox.create_or_validate(plan.effect,tx);
    if(!plan.no_op && !staged) return staged.error();
    auto transition = plan.no_op ? m_claims.complete_render_intent(id,token,row_revision,now,tx) : m_claims.plan_render_intent(id,token,row_revision,now,tx);
    if(!transition) return transition.error();
    return plan.no_op ? std::optional<OutboxEffect>{} : std::optional<OutboxEffect>{staged.value()};
}

ReconciliationResult AtomicReconciliation::publish(const ReconciliationPlan& plan,const RenderIntentId& id,ClaimToken token,StorageRevision row_revision,ClockTime now) const {
    auto unit=UnitOfWork::begin(m_backend,TransactionMode::ReadWrite); if(!unit) return {ReconciliationOutcome::Failed,std::nullopt,unit.error()}; auto* tx=unit.value().transaction(); if(!tx) return {ReconciliationOutcome::Failed,std::nullopt,err(ErrorCode::StorageUnavailable,"transaction is unavailable")};
    const auto staged = stage(plan,id,token,row_revision,now,*tx);
    if (!staged) {
        unit.value().rollback();
        const auto conflict = staged.error().code == ErrorCode::StorageConflict || staged.error().code == ErrorCode::StorageStaleClaim;
        return {conflict ? ReconciliationOutcome::RejectedConflict : ReconciliationOutcome::Failed,std::nullopt,staged.error()};
    }
    const auto commit=unit.value().commit();
    if(commit.outcome!=CommitOutcome::Committed) return {commit.outcome==CommitOutcome::RejectedConflict?ReconciliationOutcome::RejectedConflict:(commit.outcome==CommitOutcome::Unknown?ReconciliationOutcome::Unknown:ReconciliationOutcome::Failed),std::nullopt,commit.error};
    return {plan.no_op?ReconciliationOutcome::AlreadyApplied:ReconciliationOutcome::Planned,staged.value(),std::nullopt};
}

Result<ReconciliationRecovery> AtomicReconciliation::inspect_unknown(
    const ReconciliationPlan& plan, const RenderIntentId& intent_id, ClockTime now) const
{
    const auto planned_id = plan.render_intent_id.value.empty()
        ? (plan.effect.slot.render_intent_id.value.empty() ? intent_id : plan.effect.slot.render_intent_id)
        : plan.render_intent_id;
    if (!(planned_id == intent_id))
        return err(ErrorCode::StorageConflict, "plan belongs to another render intent");
    if (!plan.no_op && plan.effect.id.value.empty())
        return err(ErrorCode::InvalidArgument, "recovery plan has no effect identity");
    auto unit = UnitOfWork::begin(m_backend, TransactionMode::ReadOnly);
    if (!unit) return unit.error();
    auto* tx = unit.value().transaction();
    if (!tx) return err(ErrorCode::StorageUnavailable, "recovery transaction is unavailable");
    RenderIntentRepository intents;
    const auto intent = intents.load(intent_id, *tx);
    if (!intent) return intent.error();
    if (!intent.value()) return err(ErrorCode::StorageNotFound, "render intent is absent during recovery");
    if (plan.conversation && intent.value()->conversation != *plan.conversation)
        return err(ErrorCode::StorageConflict, "render intent conversation is stale");
    if (plan.session_revision != 0 && intent.value()->session_revision != plan.session_revision)
        return err(ErrorCode::StorageConflict, "render intent session revision is stale");
    if (plan.desired_revision != 0 && intent.value()->session_revision != plan.desired_revision)
        return err(ErrorCode::StorageConflict, "plan desired revision is stale");
    const auto claim = m_claims.inspect(intent_id, now, *tx);
    if (!claim) return claim.error();
    ReconciliationRecovery recovery;
    recovery.claim = claim.value();
    if (plan.conversation && plan.desired_generation != 0) {
        DesiredGenerationRepository generations;
        const auto marker = generations.load(plan.conversation->encode(), *tx);
        if (!marker || marker.value().value != plan.desired_generation
            || marker.value().row_revision != plan.desired_generation_row_revision) {
            recovery.error = err(ErrorCode::StorageUnknownCommit, "desired generation evidence is unavailable or stale");
            return recovery;
        }
    }
    if (!plan.target_identity.empty()) {
        AppliedTargetRepository applied;
        const auto target = applied.load(plan.target_identity, *tx);
        if (!target || (target.value() && target.value()->revision != plan.applied_target_revision)) {
            recovery.error = err(ErrorCode::StorageUnknownCommit, "applied target evidence is unavailable or stale");
            return recovery;
        }
    }
    if (plan.no_op) {
        if (!plan.target_identity.empty()) {
            AppliedTargetRepository applied;
            const auto target = applied.load(plan.target_identity, *tx);
            if (!target || !target.value()
                || target.value()->confirmed_applied_revision < plan.desired_revision
                || plan.render_policy == RenderPolicy::Always) {
                recovery.error = err(ErrorCode::StorageUnknownCommit, "no-op applied-state evidence is unavailable");
                return recovery;
            }
        }
        if (claim.value() && (claim.value()->state == RenderIntentLifecycleState::Completed
                              || claim.value()->state == RenderIntentLifecycleState::Planned)) {
            recovery.outcome = ReconciliationRecoveryOutcome::Published;
            return recovery;
        }
        if (claim.value() && claim.value()->state == RenderIntentLifecycleState::Claimed) {
            recovery.outcome = ReconciliationRecoveryOutcome::NotPublished;
            return recovery;
        }
        recovery.error = err(ErrorCode::StorageUnknownCommit, "claim state is not coherent for no-op recovery");
        return recovery;
    }
    OutboxRepository repository;
    const auto effect = repository.load(plan.effect.id, *tx);
    if (!effect) return effect.error();
    recovery.effect = effect.value();
    if (effect.value() && claim.value()
        && effect.value()->id == plan.effect.id
        && (claim.value()->state == RenderIntentLifecycleState::Planned
            || claim.value()->state == RenderIntentLifecycleState::Completed)) {
        recovery.outcome = ReconciliationRecoveryOutcome::Published;
        return recovery;
    }
    if (!effect.value() && claim.value()
        && claim.value()->state == RenderIntentLifecycleState::Claimed) {
        recovery.outcome = ReconciliationRecoveryOutcome::NotPublished;
        return recovery;
    }
    recovery.error = err(ErrorCode::StorageUnknownCommit, "intent and effect evidence is contradictory");
    return recovery;
}

} // namespace imtg
