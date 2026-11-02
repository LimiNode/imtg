#include "imtg/reconciliation/outbox.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>

namespace imtg {
namespace {
constexpr std::string_view outbox_namespace = "outbox_effects";
constexpr std::string_view applied_namespace = "applied_targets";

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
    Result<std::string> str() {
        auto n=u64(); if(!n) return n.error(); if(n.value()>m_s.size()-m_p || n.value()>1024*1024) return err(ErrorCode::StorageRecordTooLarge,"effect field is too large");
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
    std::uint64_t h = 1469598103934665603ULL;
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
    using D = ClockTime::duration;
    const auto signed_raw = static_cast<std::int64_t>(raw);
    const auto ms = std::chrono::milliseconds{signed_raw};
    return ClockTime{std::chrono::duration_cast<D>(ms)};
}

Result<void> validate_effect(const OutboxEffect& e) {
    if (e.id.value.empty() || e.slot.render_intent_id.value.empty() || e.slot.target_identity.empty()
        || e.slot.effect_position.empty() || e.payload_version == 0)
        return err(ErrorCode::InvalidArgument, "effect identity or payload is invalid");
    if (e.id.value != OutboxEffectId::from_slot(e.slot).value)
        return err(ErrorCode::InvalidArgument, "effect id does not match logical slot");
    if (static_cast<unsigned>(e.slot.effect_kind) > static_cast<unsigned>(EffectKind::Replace)
        || static_cast<unsigned>(e.state) > static_cast<unsigned>(OutboxEffectState::Superseded))
        return err(ErrorCode::InvalidArgument, "effect enum value is invalid");
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
    put_u64(s,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(e.lease_until.time_since_epoch()).count()));
    put_u64(s,static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(e.next_eligible_at.time_since_epoch()).count()));
    put_u32(s,e.attempt_count); return s;
}

Result<OutboxEffect> decode_outbox_effect(std::string_view bytes) {
    if (bytes.size()<3 || bytes.substr(0,3)!="OE1") return err(ErrorCode::UnsupportedVersion,"effect encoding version is unsupported");
    Reader r(bytes.substr(3)); OutboxEffect e;
    auto id=r.str(); auto ri=r.str(); auto target=r.str(); auto kind=r.byte(); auto pos=r.str(); auto pv=r.u32(); auto payload=r.str(); auto dg=r.str(); auto desired=r.u64();
    if(!id||!ri||!target||!kind||!pos||!pv||!payload||!dg||!desired||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.id={id.value()}; e.slot={RenderIntentId{ri.value()},target.value(),static_cast<EffectKind>(kind.value()),pos.value()}; e.payload_version=pv.value(); e.payload=payload.value(); e.payload_digest=dg.value(); e.desired_revision=desired.value();
    auto st=r.byte();
    if (!st) return st.error();
    e.state=static_cast<OutboxEffectState>(st.value()); auto token=r.u64(); auto lease=r.u64(); auto next=r.u64(); auto attempts=r.u32();
    if(!token||!lease||!next||!attempts||r.remaining()!=0) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.claim_token={token.value()}; auto lt=millis_time(lease.value()); auto nt=millis_time(next.value()); if(!lt||!nt) return err(ErrorCode::InvalidEncoding,"effect timestamp is invalid"); e.lease_until=lt.value(); e.next_eligible_at=nt.value(); e.attempt_count=attempts.value();
    const auto valid=validate_effect(e); if(!valid) return valid.error(); return e;
}

Result<std::optional<OutboxEffect>> OutboxRepository::load(const OutboxEffectId& id, ITransaction& tx) const {
    if(id.value.empty()) return err(ErrorCode::InvalidArgument,"effect id is empty");
    auto row=tx.read(outbox_namespace,id.value); if(!row) return row.error(); if(!row.value()) return std::optional<OutboxEffect>{};
    auto e=decode_outbox_effect(row.value()->value); if(!e) return e.error(); e.value().revision=row.value()->revision; return std::optional<OutboxEffect>{e.value()};
}

Result<OutboxEffect> OutboxRepository::create_or_validate(const OutboxEffect& effect, ITransaction& tx) const {
    const auto valid=validate_effect(effect); if(!valid) return valid.error();
    auto existing=load(effect.id,tx); if(!existing) return existing.error();
    if(existing.value()) {
        if(existing.value()->slot==effect.slot && existing.value()->payload_version==effect.payload_version && existing.value()->payload==effect.payload && existing.value()->payload_digest==effect.payload_digest) return *existing.value();
        return err(ErrorCode::StorageConflict,"logical effect slot already contains a different payload");
    }
    auto encoded=encode_outbox_effect(effect); if(!encoded) return encoded.error(); auto rev=tx.create_if_absent(outbox_namespace,effect.id.value,encoded.value()); if(!rev) return rev.error(); auto result=effect; result.revision=rev.value(); return result;
}

namespace {
constexpr std::string_view applied_magic = "AT1";
Result<StorageBytes> encode_applied(const AppliedTargetState& s) {
    if(s.target_identity.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty"); std::string b{applied_magic}; put_string(b,s.target_identity); b.push_back(s.physical_identity?'\x01':'\x00'); if(s.physical_identity) put_string(b,*s.physical_identity); put_u64(b,s.desired_revision); put_u64(b,s.confirmed_applied_revision); b.push_back(s.last_confirmed_effect?'\x01':'\x00'); if(s.last_confirmed_effect) put_string(b,s.last_confirmed_effect->value); return b;
}
Result<AppliedTargetState> decode_applied(std::string_view b) {
    if(b.substr(0,3)!=applied_magic) return err(ErrorCode::UnsupportedVersion,"applied target version is unsupported"); Reader r(b.substr(3)); AppliedTargetState s; auto t=r.str(); if(!t||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"applied target encoding is malformed"); s.target_identity=t.value(); auto ph=r.byte(); if(!ph) return ph.error(); if(ph.value()>1) return err(ErrorCode::InvalidEncoding,"applied target marker is malformed"); if(ph.value()) { auto v=r.str(); if(!v) return v.error(); s.physical_identity=v.value(); } auto d=r.u64(); auto a=r.u64(); if(!d||!a||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"applied target encoding is malformed"); s.desired_revision=d.value(); s.confirmed_applied_revision=a.value(); auto has=r.byte(); if(!has) return has.error(); if(has.value()>1) return err(ErrorCode::InvalidEncoding,"applied effect marker is malformed"); if(has.value()) { auto x=r.str(); if(!x) return x.error(); s.last_confirmed_effect=OutboxEffectId{x.value()}; } if(r.remaining()!=0) return err(ErrorCode::InvalidEncoding,"applied target has trailing bytes"); return s;
}
}

Result<std::optional<AppliedTargetState>> AppliedTargetRepository::load(std::string_view id, ITransaction& tx) const { if(id.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty"); auto row=tx.read(applied_namespace,target_key(id)); if(!row) return row.error(); if(!row.value()) return std::optional<AppliedTargetState>{}; auto s=decode_applied(row.value()->value); if(!s) return s.error(); s.value().revision=row.value()->revision; return std::optional<AppliedTargetState>{s.value()}; }
Result<AppliedTargetState> AppliedTargetRepository::save(const AppliedTargetState& state, StorageRevision expected, ITransaction& tx) const { const auto enc=encode_applied(state); if(!enc) return enc.error(); Result<StorageRevision> rev = expected==0 ? tx.create_if_absent(applied_namespace,state.target_identity,enc.value()) : tx.write(applied_namespace,state.target_identity,enc.value(),expected); if(!rev) return rev.error(); auto out=state; out.revision=rev.value(); return out; }

Result<ReconciliationPlan> ReconciliationPlanner::build(const ReconciliationInput& in) const {
    if(in.render_intent_id.value.empty()||in.target_identity.empty()||in.effect_position.empty()||in.payload_version==0) return err(ErrorCode::InvalidArgument,"reconciliation input is incomplete");
    if(in.render_policy!=RenderPolicy::Always && in.desired_revision<=in.applied_revision) return ReconciliationPlan{true,{}};
    LogicalEffectSlot slot{in.render_intent_id,in.target_identity,in.effect_kind,in.effect_position}; OutboxEffect e; e.slot=slot; e.id=OutboxEffectId::from_slot(slot); e.payload_version=in.payload_version; e.payload=in.canonical_payload; e.payload_digest=digest(e.payload); e.desired_revision=in.desired_revision; return ReconciliationPlan{false,e};
}

ReconciliationResult AtomicReconciliation::publish(const ReconciliationPlan& plan,const RenderIntentId& id,ClaimToken token,StorageRevision row_revision,ClockTime now) const {
    auto unit=UnitOfWork::begin(m_backend,TransactionMode::ReadWrite); if(!unit) return {ReconciliationOutcome::Failed,std::nullopt,unit.error()}; auto* tx=unit.value().transaction(); if(!tx) return {ReconciliationOutcome::Failed,std::nullopt,err(ErrorCode::StorageUnavailable,"transaction is unavailable")};
    Result<OutboxEffect> staged = plan.no_op ? OutboxEffect{} : m_outbox.create_or_validate(plan.effect,*tx);
    if(!plan.no_op && !staged) { unit.value().rollback(); return {staged.error().code==ErrorCode::StorageConflict?ReconciliationOutcome::RejectedConflict:ReconciliationOutcome::Failed,std::nullopt,staged.error()}; }
    auto transition = plan.no_op ? m_claims.complete_render_intent(id,token,row_revision,now,*tx) : m_claims.plan_render_intent(id,token,row_revision,now,*tx);
    if(!transition) { unit.value().rollback(); return {transition.error().code==ErrorCode::StorageStaleClaim?ReconciliationOutcome::RejectedConflict:ReconciliationOutcome::Failed,std::nullopt,transition.error()}; }
    const auto commit=unit.value().commit(); if(commit.outcome!=CommitOutcome::Committed) return {commit.outcome==CommitOutcome::RejectedConflict?ReconciliationOutcome::RejectedConflict:(commit.outcome==CommitOutcome::Unknown?ReconciliationOutcome::Unknown:ReconciliationOutcome::Failed),std::nullopt,commit.error};
    return {plan.no_op?ReconciliationOutcome::AlreadyApplied:ReconciliationOutcome::Planned,plan.no_op?std::optional<OutboxEffect>{}:std::optional<OutboxEffect>{staged.value()},std::nullopt};
}

} // namespace imtg
