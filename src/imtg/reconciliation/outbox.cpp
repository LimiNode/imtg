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

Result<void> validate_plan_context(const ReconciliationPlan& plan)
{
    const auto& context = plan.context;
    if (context.render_intent_id.value.empty() || plan.render_intent_id.value.empty()
        || context.conversation.encode().empty() || context.session_revision == 0
        || context.desired_revision == 0 || context.desired_generation == 0
        || context.desired_generation_row_revision == 0 || context.target_identity.empty()
        || context.effect_position.empty() || context.claim_token.value == 0
        || context.claim_row_revision == 0) {
        return err(ErrorCode::InvalidArgument, "reconciliation context is incomplete");
    }
    if (!(context.render_intent_id == plan.render_intent_id))
        return err(ErrorCode::StorageConflict, "reconciliation context intent is stale");
    if (plan.conversation && *plan.conversation != context.conversation)
        return err(ErrorCode::StorageConflict, "reconciliation context conversation is stale");
    if (context.session_revision != plan.session_revision
        || context.desired_generation != plan.desired_generation
        || context.desired_generation_row_revision != plan.desired_generation_row_revision) {
        return err(ErrorCode::StorageConflict, "reconciliation context fencing fields are stale");
    }
    if (context.target_identity.size() > max_logical_slot_size
        || context.effect_position.size() > max_logical_slot_size) {
        return err(ErrorCode::StorageRecordTooLarge, "reconciliation context is too large");
    }
    if (context.target_identity != plan.target_identity
        || context.effect_kind != plan.effect_kind
        || context.effect_position != plan.effect_position
        || context.desired_revision != plan.desired_revision
        || context.confirmed_applied_revision != plan.applied_revision
        || context.applied_target_revision != plan.applied_target_revision
        || context.render_policy != plan.render_policy) {
        return err(ErrorCode::StorageConflict, "reconciliation context does not match the plan");
    }
    if (!plan.no_op && (!(plan.effect.slot.render_intent_id == plan.render_intent_id)
        || plan.effect.slot.target_identity != plan.target_identity
        || plan.effect.slot.effect_kind != plan.effect_kind
        || plan.effect.slot.effect_position != plan.effect_position
        || plan.effect.desired_revision != plan.desired_revision)) {
        return err(ErrorCode::StorageConflict, "plan effect identity does not match context");
    }
    return {};
}

bool same_immutable_effect(const OutboxEffect& left, const OutboxEffect& right) noexcept
{
    return left.id == right.id && left.slot == right.slot
        && left.payload_version == right.payload_version
        && left.payload == right.payload
        && left.payload_digest == right.payload_digest
        && left.desired_revision == right.desired_revision;
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
        if (state.desired_revision < current.value()->desired_revision)
            return err(ErrorCode::StorageConflict, "desired revision cannot decrease");
        if (state.confirmed_applied_revision < current.value()->confirmed_applied_revision)
            return err(ErrorCode::StorageConflict, "confirmed revision cannot decrease");
    }
    const auto enc=encode_applied(state); if(!enc) return enc.error();
    Result<StorageRevision> rev = expected==0 ? tx.create_if_absent(applied_namespace,state.target_identity,enc.value()) : tx.write(applied_namespace,state.target_identity,enc.value(),expected);
    if(!rev) return rev.error(); auto out=state; out.revision=rev.value(); return out;
}

Result<ReconciliationPlan> ReconciliationPlanner::build(const ReconciliationInput& in) const {
    if(in.render_intent_id.value.empty()||in.target_identity.empty()||in.effect_position.empty()||in.payload_version==0) return err(ErrorCode::InvalidArgument,"reconciliation input is incomplete");
    if (!in.context) return err(ErrorCode::InvalidArgument, "publishable reconciliation context is required");
    if (in.render_intent_id.value.size() > max_logical_slot_size || in.target_identity.size() > max_logical_slot_size
        || in.effect_position.size() > max_logical_slot_size || in.canonical_payload.size() > max_field_size)
        return err(ErrorCode::StorageRecordTooLarge, "reconciliation input is too large");
    if (in.session_revision == 0 && in.conversation)
        return err(ErrorCode::InvalidArgument, "session revision is required with a conversation");
    if (in.desired_generation != 0 && in.desired_generation_row_revision == 0)
        return err(ErrorCode::InvalidArgument, "desired generation row revision is required");
    if (in.context->render_intent_id.value != in.render_intent_id.value
        || (in.conversation && *in.conversation != in.context->conversation)
        || in.context->target_identity != in.target_identity
        || in.context->effect_kind != in.effect_kind
        || in.context->effect_position != in.effect_position
        || in.context->session_revision != in.session_revision
        || in.context->desired_revision != in.desired_revision
        || in.context->desired_generation != in.desired_generation
        || in.context->desired_generation_row_revision != in.desired_generation_row_revision
        || in.context->applied_target_revision != in.applied_target_revision
        || in.context->confirmed_applied_revision != in.applied_revision
        || in.context->render_policy != in.render_policy) {
        return err(ErrorCode::StorageConflict, "reconciliation context does not match input");
    }
    if (in.context->conversation.encode().empty() || in.context->session_revision == 0
        || in.context->desired_revision == 0 || in.context->desired_generation == 0
        || in.context->desired_generation_row_revision == 0
        || in.context->target_identity.empty() || in.context->effect_position.empty()
        || in.context->claim_token.value == 0 || in.context->claim_row_revision == 0) {
        return err(ErrorCode::InvalidArgument, "reconciliation context is incomplete");
    }
    LogicalEffectSlot slot{in.render_intent_id,in.target_identity,in.effect_kind,in.effect_position};
    ReconciliationPlan plan;
    plan.render_intent_id = in.render_intent_id;
    plan.target_identity = in.target_identity;
    plan.effect_kind = in.effect_kind;
    plan.effect_position = in.effect_position;
    plan.desired_revision = in.desired_revision;
    plan.applied_revision = in.applied_revision;
    plan.render_policy = in.render_policy;
    plan.conversation = in.context->conversation;
    plan.session_revision = in.session_revision;
    plan.desired_generation = in.desired_generation;
    plan.desired_generation_row_revision = in.desired_generation_row_revision;
    plan.applied_target_revision = in.applied_target_revision;
    plan.context = *in.context;
    if(in.render_policy!=RenderPolicy::Always && in.desired_revision<=in.applied_revision) {
        plan.no_op = true;
        return plan;
    }
    OutboxEffect e; e.slot=slot; e.id=OutboxEffectId::from_slot(slot); e.payload_version=in.payload_version; e.payload=in.canonical_payload; e.payload_digest=digest(e.payload); e.desired_revision=in.desired_revision; plan.effect = std::move(e); return plan;
}

Result<std::optional<OutboxEffect>> AtomicReconciliation::stage(
    const ReconciliationPlan& plan, const RenderIntentId& id, ClaimToken token,
    StorageRevision row_revision, ClockTime now, ITransaction& tx) const {
    const auto reject = [&tx](Error error) -> Result<std::optional<OutboxEffect>> {
        tx.rollback();
        return error;
    };
    if (tx.mode() != TransactionMode::ReadWrite) {
        return reject(err(ErrorCode::StorageReadOnly, "TX2 requires a read-write transaction"));
    }
    const auto context = validate_plan_context(plan);
    if (!context) return reject(context.error());
    if (!(plan.context.render_intent_id == id)
        || plan.context.claim_token != token
        || plan.context.claim_row_revision != row_revision) {
        return reject(err(ErrorCode::StorageConflict, "TX2 attempt does not match plan context"));
    }
    RenderIntentRepository intents;
    const auto intent = intents.load(id, tx);
    if (!intent) return reject(intent.error());
    if (!intent.value()) return reject(err(ErrorCode::StorageNotFound, "render intent is absent"));
    if (!(intent.value()->conversation == plan.context.conversation))
        return reject(err(ErrorCode::StorageConflict, "render intent conversation is stale"));
    if (intent.value()->session_revision != plan.context.session_revision
        || intent.value()->session_revision != plan.context.desired_revision) {
        return reject(err(ErrorCode::StorageConflict, "render intent session revision is stale"));
    }

    SessionRepository sessions;
    const auto session = sessions.load(plan.context.conversation, tx);
    if (!session) return reject(session.error());
    if (!session.value() || session.value()->revision != plan.context.session_revision)
        return reject(err(ErrorCode::StorageConflict, "session revision is stale"));
    const auto watched_session = tx.watch("sessions", plan.context.conversation.encode(),
                                         plan.context.session_revision);
    if (!watched_session) return reject(watched_session.error());

    DesiredGenerationRepository generations;
    const auto marker = generations.load(plan.context.conversation.encode(), tx);
    if (!marker) return reject(marker.error());
    if (marker.value().value != plan.context.desired_generation
        || marker.value().row_revision != plan.context.desired_generation_row_revision) {
        return reject(err(ErrorCode::StorageConflict, "desired generation is stale"));
    }
    const auto watched_generation = tx.watch("render_intent_generations",
                                             plan.context.conversation.encode(),
                                             plan.context.desired_generation_row_revision);
    if (!watched_generation) return reject(watched_generation.error());

    AppliedTargetRepository applied;
    const auto state = applied.load(plan.context.target_identity, tx);
    if (!state) return reject(state.error());
    if (state.value()) {
        if (state.value()->revision != plan.context.applied_target_revision
            || state.value()->confirmed_applied_revision != plan.context.confirmed_applied_revision) {
            return reject(err(ErrorCode::StorageConflict, "applied target state is stale"));
        }
    } else if (plan.context.applied_target_revision != 0
               || plan.context.confirmed_applied_revision != 0 || plan.no_op) {
        return reject(err(ErrorCode::StorageConflict, "applied target state is incomplete"));
    }
    const auto watched_target = tx.watch("applied_targets", plan.context.target_identity,
                                         plan.context.applied_target_revision);
    if (!watched_target) return reject(watched_target.error());
    if (plan.no_op && (plan.context.render_policy == RenderPolicy::Always
                       || !state.value()
                       || state.value()->confirmed_applied_revision < plan.context.desired_revision)) {
        return reject(err(ErrorCode::StorageConflict, "no-op plan lacks applied-state proof"));
    }

    Result<OutboxEffect> staged = plan.no_op ? OutboxEffect{} : m_outbox.create_or_validate(plan.effect, tx);
    if (!plan.no_op && !staged) return reject(staged.error());
    const auto transition = plan.no_op
        ? m_claims.complete_render_intent(id, token, row_revision, now, tx)
        : m_claims.plan_render_intent(id, token, row_revision, now, tx);
    if (!transition) return reject(transition.error());
    return plan.no_op ? std::optional<OutboxEffect>{}
                      : std::optional<OutboxEffect>{staged.value()};
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
    const auto valid_context = validate_plan_context(plan);
    if (!valid_context) return valid_context.error();
    if (!(plan.context.render_intent_id == intent_id))
        return err(ErrorCode::StorageConflict, "plan belongs to another render intent");
    auto unit = UnitOfWork::begin(m_backend, TransactionMode::ReadOnly);
    if (!unit) return unit.error();
    auto* tx = unit.value().transaction();
    if (!tx) return err(ErrorCode::StorageUnavailable, "recovery transaction is unavailable");
    RenderIntentRepository intents;
    const auto intent = intents.load(intent_id, *tx);
    if (!intent) return intent.error();
    if (!intent.value()) return err(ErrorCode::StorageNotFound, "render intent is absent during recovery");
    if (!(intent.value()->conversation == plan.context.conversation))
        return err(ErrorCode::StorageConflict, "render intent conversation is stale");
    if (intent.value()->session_revision != plan.context.session_revision
        || intent.value()->session_revision != plan.context.desired_revision)
        return err(ErrorCode::StorageConflict, "render intent session revision is stale");
    SessionRepository sessions;
    const auto session = sessions.load(plan.context.conversation, *tx);
    if (!session) return session.error();
    if (!session.value() || session.value()->revision != plan.context.session_revision)
        return err(ErrorCode::StorageUnknownCommit, "session evidence is unavailable or stale");
    const auto claim = m_claims.inspect(intent_id, now, *tx);
    if (!claim) return claim.error();
    ReconciliationRecovery recovery;
    recovery.claim = claim.value();
    const auto inconclusive = [&recovery](const char* message) -> Result<ReconciliationRecovery> {
        recovery.outcome = ReconciliationRecoveryOutcome::Inconclusive;
        recovery.error = err(ErrorCode::StorageUnknownCommit, message);
        return recovery;
    };
    if (!claim.value()) return inconclusive("claim evidence is unavailable");
    if (claim.value()->claim_token != plan.context.claim_token)
        return inconclusive("claim token no longer identifies the original attempt");
    const auto expected_claim_revision = next_storage_revision(plan.context.claim_row_revision);
    if (!expected_claim_revision)
        return inconclusive("claim revision evidence is exhausted");

    DesiredGenerationRepository generations;
    const auto marker = generations.load(plan.context.conversation.encode(), *tx);
    if (!marker || marker.value().value != plan.context.desired_generation
        || marker.value().row_revision != plan.context.desired_generation_row_revision) {
        return inconclusive("desired generation evidence is unavailable or stale");
    }
    AppliedTargetRepository applied;
    const auto target = applied.load(plan.context.target_identity, *tx);
    if (!target) return inconclusive("applied target evidence is unavailable");
    if (target.value()) {
        if (target.value()->revision != plan.context.applied_target_revision
            || target.value()->confirmed_applied_revision != plan.context.confirmed_applied_revision) {
            return inconclusive("applied target evidence is stale");
        }
    } else if (plan.context.applied_target_revision != 0
               || plan.context.confirmed_applied_revision != 0) {
        return inconclusive("absent applied target contradicts the planning snapshot");
    }
    if (plan.no_op) {
        if (!target.value() || target.value()->confirmed_applied_revision < plan.context.desired_revision
            || plan.context.render_policy == RenderPolicy::Always) {
            return inconclusive("no-op applied-state evidence is unavailable");
        }
        if (claim.value()->state == RenderIntentLifecycleState::Completed
            && claim.value()->row_revision == expected_claim_revision.value()) {
            recovery.outcome = ReconciliationRecoveryOutcome::Published;
            return recovery;
        }
        if (claim.value()->state == RenderIntentLifecycleState::Claimed
            && claim.value()->row_revision == plan.context.claim_row_revision) {
            recovery.outcome = ReconciliationRecoveryOutcome::NotPublished;
            return recovery;
        }
        return inconclusive("no-op claim state is not attributable to the original attempt");
    }
    const auto effect = m_outbox.load(plan.effect.id, *tx);
    if (!effect) return effect.error();
    recovery.effect = effect.value();
    if (effect.value() && !same_immutable_effect(*effect.value(), plan.effect))
        return inconclusive("stored effect does not match the original immutable payload");
    if (effect.value()
        && (claim.value()->state == RenderIntentLifecycleState::Planned
            || claim.value()->state == RenderIntentLifecycleState::Completed)
        && claim.value()->row_revision == expected_claim_revision.value()) {
        recovery.outcome = ReconciliationRecoveryOutcome::Published;
        return recovery;
    }
    if (!effect.value() && claim.value()->state == RenderIntentLifecycleState::Claimed
        && claim.value()->row_revision == plan.context.claim_row_revision) {
        recovery.outcome = ReconciliationRecoveryOutcome::NotPublished;
        return recovery;
    }
    return inconclusive("intent and effect evidence is contradictory or not attributable");
}

} // namespace imtg
