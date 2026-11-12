#include "imtg/reconciliation/outbox.hpp"
#include "imtg/storage/time_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace imtg {
namespace {
constexpr std::string_view outbox_namespace = "outbox_effects";
constexpr std::string_view applied_namespace = "applied_targets";
constexpr std::string_view dispatch_namespace = "outbox_dispatch_targets";
constexpr std::string_view render_namespace = "render_intents";
constexpr std::string_view session_namespace = "sessions";
constexpr std::string_view generation_namespace = "render_intent_generations";
constexpr std::size_t max_field_size = 1024 * 1024;
constexpr std::size_t max_record_size = 4 * 1024 * 1024;
constexpr std::size_t max_logical_slot_size = 4096;
constexpr std::string_view logical_target_magic = "LT1";
constexpr std::string_view outbox_cursor_magic = "OBC1";

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

Result<ClockTime> add_lease(ClockTime now, std::chrono::milliseconds lease)
{
    if (lease.count() <= 0) return err(ErrorCode::InvalidArgument, "lease must be positive");
    using Duration = ClockTime::duration;
    const long double requested_ticks = static_cast<long double>(lease.count())
        * static_cast<long double>(std::milli::num)
        * static_cast<long double>(Duration::period::den)
        / static_cast<long double>(std::milli::den)
        / static_cast<long double>(Duration::period::num);
    if (!std::isfinite(requested_ticks)
        || requested_ticks >= static_cast<long double>(std::numeric_limits<Duration::rep>::max()))
        return err(ErrorCode::InvalidArgument, "lease exceeds clock range");
    const auto delta = static_cast<Duration::rep>(std::ceil(requested_ticks));
    if (delta <= 0 || now.time_since_epoch().count()
                         > std::numeric_limits<Duration::rep>::max() - delta)
        return err(ErrorCode::InvalidArgument, "lease exceeds clock range");
    const auto result = ClockTime{Duration{now.time_since_epoch().count() + delta}};
    const auto persisted = time_millis(result);
    if (!persisted) return persisted.error();
    return decode_storage_timestamp(static_cast<std::uint64_t>(persisted.value()));
}

std::string encode_cursor(std::string_view key)
{
    std::string result{outbox_cursor_magic};
    put_string(result, key);
    return result;
}

Result<std::string> decode_cursor(std::string_view encoded)
{
    if (encoded.empty()) return std::string{};
    if (encoded.size() < outbox_cursor_magic.size()
        || encoded.substr(0, outbox_cursor_magic.size()) != outbox_cursor_magic)
        return err(ErrorCode::UnsupportedVersion, "outbox continuation version is unsupported");
    Reader reader(encoded.substr(outbox_cursor_magic.size()));
    const auto key = reader.str();
    if (!key || key.value().empty() || reader.remaining() != 0)
        return err(ErrorCode::InvalidEncoding, "outbox continuation is malformed");
    return key.value();
}

bool live_claim(const OutboxEffect& effect, ClockTime now) noexcept
{
    return (effect.state == OutboxEffectState::ClaimedBeforeDispatch
            || effect.state == OutboxEffectState::DispatchAdmitted)
        && now < effect.lease_until;
}

void put_i64(std::string& s, std::int64_t value) {
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    put_u64(s, bits);
}

Result<void> validate_effect(const OutboxEffect& e) {
    if (e.id.value.empty() || e.slot.render_intent_id.value.empty() || e.slot.target_identity.empty()
        || e.slot.effect_position.empty() || e.payload_version == 0 || e.desired_revision == 0)
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
    if ((e.state == OutboxEffectState::ClaimedBeforeDispatch
         || e.state == OutboxEffectState::DispatchAdmitted
         || e.state == OutboxEffectState::UnknownOutcome
         || e.state == OutboxEffectState::Succeeded)
        && e.claim_token.value == 0)
        return err(ErrorCode::InvalidState, "active effect has no fencing token");
    if (e.state == OutboxEffectState::Superseded
        && (!e.superseded_by || e.superseded_by->value.empty()))
        return err(ErrorCode::InvalidState, "superseded effect has no successor");
    if (e.state != OutboxEffectState::Superseded && e.superseded_by)
        return err(ErrorCode::InvalidState, "non-superseded effect has a successor");
    if (e.superseded_by && e.superseded_by->value.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "successor effect identity is too large");
    if (e.slot.encode().size() > max_logical_slot_size || e.id.value.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical effect identity is too large");
    if (e.payload.size() > max_field_size || e.payload_digest.size() > max_field_size)
        return err(ErrorCode::StorageRecordTooLarge, "effect payload is too large");
    const auto target = decode_logical_target_identity(e.slot.target_identity);
    if (!target) return target.error();
    if (e.payload_digest != digest(e.payload)) return err(ErrorCode::PayloadVersionMismatch, "payload digest mismatch");
    return {};
}

Result<std::string> encode_logical_target(const LogicalTargetIdentity& target)
{
    if (target.logical_slot.empty())
        return err(ErrorCode::InvalidArgument, "logical target slot is empty");
    if (target.logical_slot.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical target slot is too large");
    const auto conversation = target.conversation.encode();
    if (conversation.empty() || conversation.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "conversation identity is too large");
    std::string encoded{logical_target_magic};
    put_string(encoded, conversation);
    put_string(encoded, target.logical_slot);
    if (encoded.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical target identity is too large");
    return encoded;
}

Result<void> validate_plan_context(const ReconciliationPlan& plan)
{
    const auto& context = plan.context;
    const auto target = decode_logical_target_identity(context.target_identity);
    if (!target) return target.error();
    if (target.value().conversation != context.conversation)
        return err(ErrorCode::ScopeMismatch, "target identity belongs to another conversation");
    if (static_cast<unsigned>(context.effect_kind) > static_cast<unsigned>(EffectKind::Replace))
        return err(ErrorCode::InvalidArgument, "effect kind is invalid");
    if (static_cast<unsigned>(context.render_policy) > static_cast<unsigned>(RenderPolicy::Always)
        || context.render_policy == RenderPolicy::Never)
        return err(ErrorCode::InvalidArgument, "render policy cannot publish reconciliation");
    if (context.render_policy == RenderPolicy::Always
        && context.confirmed_applied_revision > context.desired_revision)
        return err(ErrorCode::StorageConflict,
                   "always-render plan is older than confirmed applied state");
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

Result<std::string> LogicalTargetIdentity::encode() const
{
    return encode_logical_target(*this);
}

Result<std::string> make_logical_target_identity(
    const ConversationKey& conversation, std::string_view logical_slot)
{
    return LogicalTargetIdentity{conversation, std::string(logical_slot)}.encode();
}

Result<LogicalTargetIdentity> decode_logical_target_identity(std::string_view encoded)
{
    if (encoded.size() < logical_target_magic.size()
        || encoded.substr(0, logical_target_magic.size()) != logical_target_magic)
        return err(ErrorCode::UnsupportedVersion, "logical target identity version is unsupported");
    if (encoded.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "logical target identity is too large");
    Reader reader(encoded.substr(logical_target_magic.size()));
    const auto conversation = reader.str();
    const auto slot = reader.str();
    if (!conversation || !slot || reader.remaining() != 0)
        return err(ErrorCode::InvalidEncoding, "logical target identity is malformed");
    const auto parsed = parse_conversation_key(conversation.value());
    if (!parsed) return parsed.error();
    LogicalTargetIdentity result{parsed.value(), slot.value()};
    if (result.logical_slot.empty())
        return err(ErrorCode::InvalidEncoding, "logical target slot is empty");
    const auto canonical = result.encode();
    if (!canonical || canonical.value() != encoded)
        return canonical ? Result<LogicalTargetIdentity>(err(ErrorCode::InvalidEncoding,
                                                              "logical target identity is not canonical"))
                         : Result<LogicalTargetIdentity>(canonical.error());
    return result;
}

OutboxEffectId OutboxEffectId::from_slot(const LogicalEffectSlot& slot) {
    const auto bytes = slot.encode();
    // Encode the complete canonical tuple rather than a digest: payload is not
    // part of identity, and a hash-only identifier would make collisions
    // indistinguishable logical slots.
    return OutboxEffectId{std::string("OE1:") + hex_encode(bytes)};
}

Result<StorageBytes> encode_outbox_effect(const OutboxEffect& e) {
    const auto valid = validate_effect(e); if (!valid) return valid.error();
    std::string s{"OE2"};
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
    s.push_back(e.superseded_by ? '\x01' : '\x00');
    if (e.superseded_by) put_string(s, e.superseded_by->value);
    if (s.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge, "effect record is too large");
    return s;
}

Result<OutboxEffect> decode_outbox_effect(std::string_view bytes) {
    if (bytes.size() < 3
        || (bytes.substr(0, 3) != "OE1" && bytes.substr(0, 3) != "OE2"))
        return err(ErrorCode::UnsupportedVersion,"effect encoding version is unsupported");
    if (bytes.size() > max_record_size) return err(ErrorCode::StorageRecordTooLarge,"effect record is too large");
    Reader r(bytes.substr(3)); OutboxEffect e;
    auto id=r.str(); auto ri=r.str(); auto target=r.str(); auto kind=r.byte(); auto pos=r.str(); auto pv=r.u32(); auto payload=r.str(); auto dg=r.str(); auto desired=r.u64();
    if(!id||!ri||!target||!kind||!pos||!pv||!payload||!dg||!desired||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.id={id.value()}; e.slot={RenderIntentId{ri.value()},target.value(),static_cast<EffectKind>(kind.value()),pos.value()}; e.payload_version=pv.value(); e.payload=payload.value(); e.payload_digest=dg.value(); e.desired_revision=desired.value();
    auto st=r.byte();
    if (!st) return st.error();
    e.state=static_cast<OutboxEffectState>(st.value()); auto token=r.u64(); auto lease=r.i64(); auto next=r.i64(); auto attempts=r.u32();
    if(!token||!lease||!next||!attempts) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
    e.claim_token={token.value()};
    std::uint64_t lease_bits{}, next_bits{};
    std::memcpy(&lease_bits, &lease.value(), sizeof(lease_bits));
    std::memcpy(&next_bits, &next.value(), sizeof(next_bits));
    auto lt=millis_time(lease_bits); auto nt=millis_time(next_bits); if(!lt||!nt) return err(ErrorCode::InvalidEncoding,"effect timestamp is invalid"); e.lease_until=lt.value(); e.next_eligible_at=nt.value(); e.attempt_count=attempts.value();
    if (bytes.substr(0, 3) == "OE2") {
        const auto has_successor = r.byte();
        if (!has_successor || has_successor.value() > 1)
            return err(ErrorCode::InvalidEncoding, "successor effect marker is malformed");
        if (has_successor.value() != 0) {
            const auto successor = r.str();
            if (!successor || successor.value().empty())
                return err(ErrorCode::InvalidEncoding, "successor effect identity is malformed");
            e.superseded_by = OutboxEffectId{successor.value()};
        }
    }
    if (r.remaining() != 0) return err(ErrorCode::InvalidEncoding,"effect encoding is malformed");
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
        || effect.attempt_count != 0 || effect.lease_until != ClockTime{})
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

Result<OutboxEffect> load_required_effect(const OutboxEffectId& id, ITransaction& transaction)
{
    if (id.value.empty()) return err(ErrorCode::InvalidArgument, "effect id is empty");
    if (id.value.size() > max_logical_slot_size)
        return err(ErrorCode::StorageRecordTooLarge, "effect id is too large");
    const auto row = transaction.read(outbox_namespace, id.value);
    if (!row) return row.error();
    if (!row.value()) return err(ErrorCode::StorageNotFound, "outbox effect is absent");
    const auto decoded = decode_outbox_effect(row.value()->value);
    if (!decoded) return decoded.error();
    if (decoded.value().id.value != id.value)
        return err(ErrorCode::StorageSerializationFailure,
                   "effect identity does not match storage key");
    auto result = decoded.value();
    result.revision = row.value()->revision;
    return result;
}

Result<void> watch_delivery_target(const OutboxEffect& effect, ITransaction& transaction,
                                   bool require_current)
{
    AppliedTargetRepository applied;
    const auto target = applied.load(effect.slot.target_identity, transaction);
    if (!target) return target.error();
    if (require_current && target.value()
        && (target.value()->desired_revision > effect.desired_revision
            || target.value()->confirmed_applied_revision > effect.desired_revision))
        return err(ErrorCode::StorageStaleClaim, "effect is older than authoritative target state");
    return transaction.watch(applied_namespace, effect.slot.target_identity,
                             target.value() ? target.value()->revision : 0);
}

Result<void> watch_delivery_context(const OutboxEffect& effect, ITransaction& transaction)
{
    const auto target = decode_logical_target_identity(effect.slot.target_identity);
    if (!target) return target.error();

    RenderIntentRepository intents;
    const auto intent = intents.load(effect.slot.render_intent_id, transaction);
    if (!intent) return intent.error();
    if (!intent.value()) return err(ErrorCode::StorageNotFound,
                                   "delivery render intent is absent");
    if (target.value().conversation != intent.value()->conversation)
        return err(ErrorCode::ScopeMismatch,
                   "delivery target belongs to another render intent conversation");
    if (intent.value()->session_revision != effect.desired_revision)
        return err(ErrorCode::StorageStaleClaim,
                   "delivery effect does not match render intent revision");

    const auto intent_row = transaction.read(render_namespace, effect.slot.render_intent_id.value);
    if (!intent_row) return intent_row.error();
    if (!intent_row.value()) return err(ErrorCode::StorageNotFound,
                                        "delivery render intent row is absent");
    const auto watched_intent = transaction.watch(render_namespace,
                                                  effect.slot.render_intent_id.value,
                                                  intent_row.value()->revision);
    if (!watched_intent) return watched_intent.error();

    SessionRepository sessions;
    const auto session = sessions.load(intent.value()->conversation, transaction);
    if (!session) return session.error();
    if (!session.value()) return err(ErrorCode::StorageNotFound,
                                     "delivery session is absent");
    if (session.value()->revision != intent.value()->session_revision)
        return err(ErrorCode::StorageStaleClaim,
                   "delivery effect is older than the current session");
    const auto watched_session = transaction.watch(session_namespace,
                                                   intent.value()->conversation.encode(),
                                                   session.value()->revision);
    if (!watched_session) return watched_session.error();

    DesiredGenerationRepository generations;
    const auto generation = generations.load(intent.value()->conversation.encode(), transaction);
    if (!generation) return generation.error();
    if (generation.value().value == 0)
        return err(ErrorCode::StorageSerializationFailure,
                   "desired generation marker is invalid");
    const auto watched_generation = transaction.watch(
        generation_namespace, intent.value()->conversation.encode(),
        generation.value().row_revision);
    if (!watched_generation) return watched_generation.error();
    return {};
}

// The target slot survives lease expiry: an admitted/unknown operation may still
// execute remotely. Only a definite outcome or explicit resolution releases it.
enum class DispatchOwnership { Acquire, Release, Watch };

Result<void> change_dispatch_owner(const OutboxEffect& effect, ITransaction& transaction,
                                   DispatchOwnership operation)
{
    const auto row = transaction.read(dispatch_namespace, effect.slot.target_identity);
    if (!row) return row.error();
    std::string owner;
    ClaimToken token;
    if (row.value()) {
        if (row.value()->value.substr(0, 3) != "DT1")
            return err(ErrorCode::UnsupportedVersion, "dispatch target version is unsupported");
        Reader reader(std::string_view(row.value()->value).substr(3));
        const auto id = reader.str();
        const auto stored_token = reader.u64();
        if (!id || !stored_token || reader.remaining() != 0
            || id.value().size() > max_logical_slot_size
            || (id.value().empty() != (stored_token.value() == 0)))
            return err(ErrorCode::InvalidEncoding, "dispatch target owner is malformed");
        owner = id.value();
        token = ClaimToken{stored_token.value()};
    }
    const bool acquire = operation == DispatchOwnership::Acquire;
    if (acquire ? !owner.empty() : (owner != effect.id.value || token != effect.claim_token))
        return err(ErrorCode::StorageConflict, "dispatch target belongs to another attempt");
    if (operation == DispatchOwnership::Watch)
        return transaction.watch(dispatch_namespace, effect.slot.target_identity, row.value()->revision);
    std::string encoded{"DT1"};
    put_string(encoded, acquire ? effect.id.value : std::string{});
    put_u64(encoded, acquire ? effect.claim_token.value : 0);
    const auto written = row.value()
        ? transaction.write(dispatch_namespace, effect.slot.target_identity,
                             encoded, row.value()->revision)
        : transaction.create_if_absent(dispatch_namespace, effect.slot.target_identity, encoded);
    if (!written) return written.error();
    return {};
}

Result<OutboxEffect> write_effect(OutboxEffect effect, ITransaction& transaction)
{
    const auto reject = [&transaction](Error error) -> Result<OutboxEffect> {
        transaction.rollback();
        return error;
    };
    const auto watched = watch_delivery_target(effect, transaction,
        effect.state == OutboxEffectState::ClaimedBeforeDispatch
        || effect.state == OutboxEffectState::DispatchAdmitted
        || effect.state == OutboxEffectState::Succeeded);
    if (!watched) return reject(watched.error());
    if (effect.state == OutboxEffectState::DispatchAdmitted
        || effect.state == OutboxEffectState::UnknownOutcome) {
        const auto owner = change_dispatch_owner(effect, transaction, DispatchOwnership::Watch);
        if (!owner) return reject(owner.error());
    }
    const auto valid = validate_effect(effect);
    if (!valid) return reject(valid.error());
    const auto encoded = encode_outbox_effect(effect);
    if (!encoded) return reject(encoded.error());
    const auto written = transaction.write(outbox_namespace, effect.id.value,
                                           encoded.value(), effect.revision);
    if (!written) return reject(written.error());
    effect.revision = written.value();
    return effect;
}

Result<OutboxEffect> rollback_effect(ITransaction& transaction, Error error)
{
    transaction.rollback();
    return error;
}

Result<OutboxEffect> apply_confirmed_success(
    const OutboxEffect& loaded, ClockTime now, std::optional<std::string> physical_identity,
    IAppliedTargetRepository& applied, ITransaction& transaction)
{
    if (physical_identity && physical_identity->size() > max_logical_slot_size)
        return rollback_effect(transaction, err(ErrorCode::StorageRecordTooLarge,
                                                "physical identity is too large"));

    const auto current = applied.load(loaded.slot.target_identity, transaction);
    if (!current) return rollback_effect(transaction, current.error());

    bool applied_already = false;
    AppliedTargetState next;
    if (current.value()) {
        if (current.value()->desired_revision > loaded.desired_revision
            || current.value()->confirmed_applied_revision > loaded.desired_revision)
            return rollback_effect(transaction, err(ErrorCode::StorageStaleClaim,
                                                    "acknowledgement is older than target state"));
        if (current.value()->confirmed_applied_revision == loaded.desired_revision) {
            if (current.value()->last_confirmed_effect
                && *current.value()->last_confirmed_effect == loaded.id) {
                applied_already = true;
                next = *current.value();
            } else {
                // Independent Always intents may intentionally render the same
                // session revision more than once. State-change effects at the
                // same revision remain fenced by the last confirmed identity.
                RenderIntentRepository intents;
                const auto intent = intents.load(loaded.slot.render_intent_id, transaction);
                if (!intent) return rollback_effect(transaction, intent.error());
                if (!intent.value() || !intent.value()->origin)
                    return rollback_effect(transaction, err(ErrorCode::StorageStaleClaim,
                                                            "another effect already confirmed this revision"));
                next = *current.value();
                if (loaded.slot.effect_kind == EffectKind::Delete)
                    next.physical_identity.reset();
                else if (physical_identity)
                    next.physical_identity = std::move(physical_identity);
                next.last_confirmed_effect = loaded.id;
            }
        } else {
            next = *current.value();
            next.desired_revision = std::max(next.desired_revision, loaded.desired_revision);
            next.confirmed_applied_revision = loaded.desired_revision;
            if (loaded.slot.effect_kind == EffectKind::Delete)
                next.physical_identity.reset();
            else if (physical_identity)
                next.physical_identity = std::move(physical_identity);
            next.last_confirmed_effect = loaded.id;
        }
    } else {
        if (loaded.slot.effect_kind == EffectKind::Delete) physical_identity.reset();
        next = AppliedTargetState{loaded.slot.target_identity, std::move(physical_identity),
                                  loaded.desired_revision, loaded.desired_revision,
                                  loaded.id, 0};
    }

    if (!applied_already) {
        const auto saved = applied.save(next, current.value() ? current.value()->revision : 0,
                                        transaction);
        if (!saved) return rollback_effect(transaction, saved.error());
    }
    const auto released = change_dispatch_owner(loaded, transaction, DispatchOwnership::Release);
    if (!released) return rollback_effect(transaction, released.error());
    auto acknowledged = loaded;
    acknowledged.state = OutboxEffectState::Succeeded;
    acknowledged.lease_until = ClockTime{};
    acknowledged.next_eligible_at = now;
    return write_effect(acknowledged, transaction);
}

Result<OutboxEffect> require_live_claim(const OutboxEffect& effect, ClaimToken token,
                                        StorageRevision expected_revision, ClockTime now)
{
    if (effect.revision != expected_revision || effect.claim_token != token
        || !live_claim(effect, now))
        return err(ErrorCode::StorageStaleClaim, "effect claim token or lease is stale");
    return effect;
}

Result<OutboxEffect> require_token(const OutboxEffect& effect, ClaimToken token,
                                   StorageRevision expected_revision)
{
    if (effect.revision != expected_revision || effect.claim_token != token)
        return err(ErrorCode::StorageStaleClaim, "effect claim token or revision is stale");
    return effect;
}

Result<void> validate_retry_time(ClockTime now, ClockTime next_eligible_at)
{
    if (next_eligible_at < now)
        return err(ErrorCode::InvalidArgument, "retry time must not be in the past");
    if (!time_millis(next_eligible_at)) return time_millis(next_eligible_at).error();
    return {};
}

} // namespace

Result<OutboxEffectPage> OutboxRepository::claim_outbox_effects(
    std::size_t limit, ClockTime now, std::chrono::milliseconds lease_duration,
    std::string_view continuation, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite) {
        transaction.rollback();
        return err(ErrorCode::StorageReadOnly, "outbox claim requires a read-write transaction");
    }
    if (limit == 0 || limit > 4096 || lease_duration.count() <= 0
        || m_page_size == 0 || m_page_size > 4096 || m_examination_budget == 0
        || m_max_attempts == 0) {
        transaction.rollback();
        return err(ErrorCode::InvalidArgument, "outbox claim bounds are invalid");
    }
    const auto decoded_cursor = decode_cursor(continuation);
    if (!decoded_cursor) {
        transaction.rollback();
        return decoded_cursor.error();
    }

    OutboxEffectPage page;
    std::string cursor = decoded_cursor.value();
    while (page.effects.size() < limit && page.examined < m_examination_budget) {
        const auto remaining = m_examination_budget - page.examined;
        const auto page_limit = std::min(m_page_size, remaining);
        const auto scanned = transaction.scan(outbox_namespace, cursor, page_limit);
        if (!scanned) {
            transaction.rollback();
            return scanned.error();
        }
        if (scanned.value().empty()) {
            page.continuation.reset();
            break;
        }
        std::size_t consumed = 0;
        for (const auto& entry : scanned.value()) {
            cursor = entry.key;
            page.continuation = encode_cursor(cursor);
            ++page.examined;
            ++consumed;

            const auto decoded = decode_outbox_effect(entry.record.value);
            if (!decoded) {
                transaction.rollback();
                return decoded.error();
            }
            auto current = decoded.value();
            current.revision = entry.record.revision;
            if (current.id.value != entry.key) {
                transaction.rollback();
                return err(ErrorCode::StorageSerializationFailure,
                           "effect identity does not match storage key");
            }
            if (current.state == OutboxEffectState::Succeeded
                || current.state == OutboxEffectState::Failed
                || current.state == OutboxEffectState::Superseded
                || current.state == OutboxEffectState::UnknownOutcome)
                continue;
            if (current.next_eligible_at > now) continue;
            if (current.state == OutboxEffectState::ClaimedBeforeDispatch
                && now < current.lease_until)
                continue;
            if (current.state == OutboxEffectState::DispatchAdmitted) {
                if (now < current.lease_until) continue;
                current.state = OutboxEffectState::UnknownOutcome;
                current.lease_until = ClockTime{};
                current.next_eligible_at = now;
                const auto recovered = write_effect(current, transaction);
                if (!recovered) {
                    transaction.rollback();
                    return recovered.error();
                }
                continue;
            }

            const auto current_target = watch_delivery_target(current, transaction, true);
            if (!current_target && current_target.error().code != ErrorCode::StorageStaleClaim) {
                transaction.rollback();
                return current_target.error();
            }
            if (!current_target || current.attempt_count >= m_max_attempts) {
                current.state = OutboxEffectState::Failed;
                current.lease_until = ClockTime{};
                const auto failed = write_effect(current, transaction);
                if (!failed) return failed.error();
                continue;
            }
            const auto next_token = next_storage_revision(current.claim_token.value);
            if (!next_token) {
                transaction.rollback();
                return next_token.error();
            }
            if (current.attempt_count == std::numeric_limits<std::uint32_t>::max()) {
                transaction.rollback();
                return err(ErrorCode::StorageRevisionExhausted,
                           "outbox attempt counter is exhausted");
            }
            const auto lease_until = add_lease(now, lease_duration);
            if (!lease_until) {
                transaction.rollback();
                return lease_until.error();
            }
            current.state = OutboxEffectState::ClaimedBeforeDispatch;
            current.claim_token = ClaimToken{next_token.value()};
            current.lease_until = lease_until.value();
            current.next_eligible_at = now;
            ++current.attempt_count;
            current.superseded_by.reset();
            const auto claimed = write_effect(current, transaction);
            if (!claimed) {
                transaction.rollback();
                return claimed.error();
            }
            page.effects.push_back(claimed.value());
            if (page.effects.size() == limit || page.examined == m_examination_budget) break;
        }
        if (consumed == scanned.value().size() && scanned.value().size() < page_limit) {
            page.continuation.reset();
            break;
        }
    }
    return page;
}

Result<OutboxEffect> OutboxRepository::renew_outbox_effect(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, ClockTime lease_until, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "outbox renewal requires a read-write transaction"));
    if (lease_until <= now || !time_millis(lease_until))
        return rollback_effect(transaction, err(ErrorCode::InvalidArgument,
                                                "effect lease expiry must be in the future"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto live = require_live_claim(loaded.value(), token, expected_revision, now);
    if (!live) return rollback_effect(transaction, live.error());
    if (lease_until < loaded.value().lease_until)
        return rollback_effect(transaction, err(ErrorCode::InvalidArgument,
                                                "lease renewal cannot shorten the current lease"));
    auto next = loaded.value();
    next.lease_until = lease_until;
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::mark_dispatch_admitted(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "dispatch admission requires a read-write transaction"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto live = require_live_claim(loaded.value(), token, expected_revision, now);
    if (!live || loaded.value().state != OutboxEffectState::ClaimedBeforeDispatch)
        return rollback_effect(transaction, live ? Error{ErrorCode::InvalidState,
                                                         "effect is not before dispatch admission"}
                                                  : live.error());
    const auto context = watch_delivery_context(loaded.value(), transaction);
    if (!context) return rollback_effect(transaction, context.error());
    auto next = loaded.value();
    next.state = OutboxEffectState::DispatchAdmitted;
    const auto acquired = change_dispatch_owner(next, transaction, DispatchOwnership::Acquire);
    if (!acquired) return rollback_effect(transaction, acquired.error());
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::acknowledge_success(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, std::optional<std::string> physical_identity,
    IAppliedTargetRepository& applied, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "acknowledgement requires a read-write transaction"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto token_match = require_token(loaded.value(), token, expected_revision);
    if (!token_match) return rollback_effect(transaction, token_match.error());
    if (loaded.value().state == OutboxEffectState::Succeeded) return loaded.value();
    const auto live = require_live_claim(loaded.value(), token, expected_revision, now);
    if (!live || loaded.value().state != OutboxEffectState::DispatchAdmitted)
        return rollback_effect(transaction, live ? Error{ErrorCode::InvalidState,
                                                         "effect is not admitted for acknowledgement"}
                                                  : live.error());
    return apply_confirmed_success(loaded.value(), now, std::move(physical_identity),
                                   applied, transaction);
}

Result<OutboxEffect> OutboxRepository::resolve_unknown_success(
    const UnknownSuccessEvidence& evidence, ClockTime now,
    IAppliedTargetRepository& applied, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "unknown success resolution requires a read-write transaction"));
    if (evidence.effect_id.value.empty() || evidence.target_identity.empty()
        || evidence.claim_token.value == 0 || evidence.expected_revision == 0)
        return rollback_effect(transaction, err(ErrorCode::InvalidArgument,
                                                "unknown success evidence is incomplete"));
    const auto target = decode_logical_target_identity(evidence.target_identity);
    if (!target) return rollback_effect(transaction, target.error());
    const auto loaded = load_required_effect(evidence.effect_id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto token_match = require_token(loaded.value(), evidence.claim_token,
                                           evidence.expected_revision);
    if (!token_match) return rollback_effect(transaction, token_match.error());
    if (loaded.value().slot.target_identity != evidence.target_identity)
        return rollback_effect(transaction, err(ErrorCode::ScopeMismatch,
                                                "unknown success target does not match the effect"));
    const auto observed_target = applied.load(evidence.target_identity, transaction);
    if (!observed_target) return rollback_effect(transaction, observed_target.error());
    if ((observed_target.value() ? observed_target.value()->revision : 0)
        != evidence.expected_target_revision)
        return rollback_effect(transaction, err(ErrorCode::StorageStaleClaim,
                                                "applied target evidence is stale"));
    if (loaded.value().state == OutboxEffectState::Succeeded) return loaded.value();
    if (loaded.value().state != OutboxEffectState::UnknownOutcome)
        return rollback_effect(transaction, err(ErrorCode::InvalidState,
                                                "effect is not in UnknownOutcome"));
    return apply_confirmed_success(loaded.value(), now, evidence.physical_identity,
                                   applied, transaction);
}

Result<OutboxEffect> OutboxRepository::record_retryable_failure(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, ClockTime next_eligible_at, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "retry transition requires a read-write transaction"));
    const auto time = validate_retry_time(now, next_eligible_at);
    if (!time) return rollback_effect(transaction, time.error());
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto live = require_live_claim(loaded.value(), token, expected_revision, now);
    if (!live || (loaded.value().state != OutboxEffectState::ClaimedBeforeDispatch
                  && loaded.value().state != OutboxEffectState::DispatchAdmitted))
        return rollback_effect(transaction, live ? Error{ErrorCode::InvalidState,
                                                         "effect is not retryable"}
                                                  : live.error());
    auto next = loaded.value();
    if (next.state == OutboxEffectState::DispatchAdmitted) {
        const auto released = change_dispatch_owner(next, transaction, DispatchOwnership::Release);
        if (!released) return rollback_effect(transaction, released.error());
    }
    next.state = next.attempt_count >= m_max_attempts
        ? OutboxEffectState::Failed : OutboxEffectState::RetryableFailure;
    next.lease_until = ClockTime{};
    next.next_eligible_at = next_eligible_at;
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::record_unknown_outcome(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "unknown transition requires a read-write transaction"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto live = require_live_claim(loaded.value(), token, expected_revision, now);
    if (!live || loaded.value().state != OutboxEffectState::DispatchAdmitted)
        return rollback_effect(transaction, live ? Error{ErrorCode::InvalidState,
                                                         "effect has no dispatch admission marker"}
                                                  : live.error());
    auto next = loaded.value();
    next.state = OutboxEffectState::UnknownOutcome;
    next.lease_until = ClockTime{};
    next.next_eligible_at = now;
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::record_failed(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "failure transition requires a read-write transaction"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto token_match = require_token(loaded.value(), token, expected_revision);
    if (!token_match) return rollback_effect(transaction, token_match.error());
    if (loaded.value().state == OutboxEffectState::Succeeded
        || loaded.value().state == OutboxEffectState::Failed
        || loaded.value().state == OutboxEffectState::Superseded)
        return rollback_effect(transaction, err(ErrorCode::InvalidState,
                                                "effect is already terminal"));
    if (loaded.value().state != OutboxEffectState::UnknownOutcome
        && !live_claim(loaded.value(), now))
        return rollback_effect(transaction, err(ErrorCode::StorageStaleClaim,
                                                "effect claim lease is stale"));
    auto next = loaded.value();
    if (next.state == OutboxEffectState::DispatchAdmitted
        || next.state == OutboxEffectState::UnknownOutcome) {
        const auto released = change_dispatch_owner(next, transaction, DispatchOwnership::Release);
        if (!released) return rollback_effect(transaction, released.error());
    }
    next.state = OutboxEffectState::Failed;
    next.lease_until = ClockTime{};
    next.next_eligible_at = now;
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::retry_unknown_effect(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, bool safe_repeat_proof, ClockTime next_eligible_at,
    ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "unknown recovery requires a read-write transaction"));
    if (!safe_repeat_proof)
        return rollback_effect(transaction, err(ErrorCode::InvalidState,
                                                "unknown effect lacks a safe-repeat proof"));
    const auto time = validate_retry_time(now, next_eligible_at);
    if (!time) return rollback_effect(transaction, time.error());
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto token_match = require_token(loaded.value(), token, expected_revision);
    if (!token_match) return rollback_effect(transaction, token_match.error());
    if (loaded.value().state != OutboxEffectState::UnknownOutcome)
        return rollback_effect(transaction, err(ErrorCode::InvalidState,
                                                "effect is not unknown"));
    auto next = loaded.value();
    const auto released = change_dispatch_owner(next, transaction, DispatchOwnership::Release);
    if (!released) return rollback_effect(transaction, released.error());
    next.state = next.attempt_count >= m_max_attempts
        ? OutboxEffectState::Failed : OutboxEffectState::RetryableFailure;
    next.next_eligible_at = next_eligible_at;
    return write_effect(next, transaction);
}

Result<OutboxEffect> OutboxRepository::supersede_effect(
    const OutboxEffectId& id, ClaimToken token, StorageRevision expected_revision,
    ClockTime now, const OutboxEffectId& newer_effect_id, ITransaction& transaction) const
{
    if (transaction.mode() != TransactionMode::ReadWrite)
        return rollback_effect(transaction, err(ErrorCode::StorageReadOnly,
                                                "supersession requires a read-write transaction"));
    if (newer_effect_id.value.empty() || newer_effect_id == id)
        return rollback_effect(transaction, err(ErrorCode::InvalidArgument,
                                                "successor effect identity is invalid"));
    const auto loaded = load_required_effect(id, transaction);
    if (!loaded) return rollback_effect(transaction, loaded.error());
    const auto token_match = require_token(loaded.value(), token, expected_revision);
    if (!token_match) return rollback_effect(transaction, token_match.error());
    if (loaded.value().state == OutboxEffectState::Succeeded
        || loaded.value().state == OutboxEffectState::Failed
        || loaded.value().state == OutboxEffectState::Superseded)
        return rollback_effect(transaction, err(ErrorCode::InvalidState,
                                                "effect cannot be superseded"));
    if (loaded.value().state != OutboxEffectState::UnknownOutcome
        && !live_claim(loaded.value(), now))
        return rollback_effect(transaction, err(ErrorCode::StorageStaleClaim,
                                                "effect claim lease is stale"));
    const auto newer = load_required_effect(newer_effect_id, transaction);
    if (!newer) return rollback_effect(transaction, newer.error());
    if (newer.value().slot.target_identity != loaded.value().slot.target_identity
        || newer.value().desired_revision <= loaded.value().desired_revision
        || newer.value().state == OutboxEffectState::Failed
        || newer.value().state == OutboxEffectState::Superseded)
        return rollback_effect(transaction, err(ErrorCode::StorageConflict,
                                                "successor effect is not newer for the same target"));
    const auto watched_newer = transaction.watch(outbox_namespace, newer_effect_id.value,
                                                  newer.value().revision);
    if (!watched_newer) return rollback_effect(transaction, watched_newer.error());
    auto next = loaded.value();
    if (next.state == OutboxEffectState::DispatchAdmitted
        || next.state == OutboxEffectState::UnknownOutcome) {
        const auto released = change_dispatch_owner(next, transaction, DispatchOwnership::Release);
        if (!released) return rollback_effect(transaction, released.error());
    }
    next.state = OutboxEffectState::Superseded;
    next.lease_until = ClockTime{};
    next.next_eligible_at = now;
    next.superseded_by = newer_effect_id;
    return write_effect(next, transaction);
}

namespace {
constexpr std::string_view applied_magic = "AT1";
Result<StorageBytes> encode_applied(const AppliedTargetState& s) {
    if(s.target_identity.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty");
    const auto target = decode_logical_target_identity(s.target_identity);
    if (!target) return target.error();
    if (s.target_identity.size() > max_logical_slot_size
        || (s.physical_identity && s.physical_identity->size() > max_logical_slot_size)
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
    if(ph.value()) {
        auto v=r.str();
        if(!v) return v.error();
        if (v.value().size() > max_logical_slot_size)
            return err(ErrorCode::StorageRecordTooLarge,
                       "physical target identity is too large");
        s.physical_identity=v.value();
    }
    auto d=r.u64(); auto a=r.u64(); if(!d||!a||r.remaining()<1) return err(ErrorCode::InvalidEncoding,"applied target encoding is malformed");
    s.desired_revision=d.value(); s.confirmed_applied_revision=a.value();
    if (s.confirmed_applied_revision > s.desired_revision)
        return err(ErrorCode::InvalidEncoding,"confirmed revision exceeds desired revision");
    auto has=r.byte(); if(!has) return has.error(); if(has.value()>1) return err(ErrorCode::InvalidEncoding,"applied effect marker is malformed");
    if(has.value()) { auto x=r.str(); if(!x) return x.error(); if (x.value().empty()) return err(ErrorCode::InvalidEncoding,"applied effect identity is empty"); s.last_confirmed_effect=OutboxEffectId{x.value()}; }
    if(r.remaining()!=0) return err(ErrorCode::InvalidEncoding,"applied target has trailing bytes"); return s;
}
}

Result<std::optional<AppliedTargetState>> AppliedTargetRepository::load(std::string_view id, ITransaction& tx) const { if(id.empty()) return err(ErrorCode::InvalidArgument,"target identity is empty"); if (id.size() > max_logical_slot_size) return err(ErrorCode::StorageRecordTooLarge,"target identity is too large"); const auto target = decode_logical_target_identity(id); if (!target) return target.error(); auto row=tx.read(applied_namespace,target_key(id)); if(!row) return row.error(); if(!row.value()) return std::optional<AppliedTargetState>{}; auto s=decode_applied(row.value()->value); if(!s) return s.error(); if(s.value().target_identity != id) return err(ErrorCode::StorageSerializationFailure,"target identity does not match storage key"); s.value().revision=row.value()->revision; return std::optional<AppliedTargetState>{s.value()}; }
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
    if (static_cast<unsigned>(in.render_policy) > static_cast<unsigned>(RenderPolicy::Always)
        || in.render_policy == RenderPolicy::Never)
        return err(ErrorCode::InvalidArgument, "render policy cannot publish reconciliation");
    if (static_cast<unsigned>(in.effect_kind) > static_cast<unsigned>(EffectKind::Replace))
        return err(ErrorCode::InvalidArgument, "effect kind is invalid");
    if (in.render_policy == RenderPolicy::Always
        && in.applied_revision > in.desired_revision)
        return err(ErrorCode::StorageConflict,
                   "always-render plan is older than confirmed applied state");
    const auto target = decode_logical_target_identity(in.target_identity);
    if (!target) return target.error();
    if (target.value().conversation != in.context->conversation)
        return err(ErrorCode::ScopeMismatch, "target identity belongs to another conversation");
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
        if (state.value()->desired_revision > plan.context.desired_revision
            || state.value()->confirmed_applied_revision > plan.context.desired_revision) {
            return reject(err(ErrorCode::StorageConflict,
                              "applied target state is newer than the plan"));
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
    ReconciliationRecovery recovery;
    const auto inconclusive = [&recovery](const char* message) -> Result<ReconciliationRecovery> {
        recovery.outcome = ReconciliationRecoveryOutcome::Inconclusive;
        recovery.error = err(ErrorCode::StorageUnknownCommit, message);
        return recovery;
    };
    if (!intent.value()) return inconclusive("render intent evidence is unavailable");
    if (!(intent.value()->conversation == plan.context.conversation))
        return inconclusive("render intent conversation evidence changed");
    if (intent.value()->session_revision != plan.context.session_revision
        || intent.value()->session_revision != plan.context.desired_revision)
        return inconclusive("render intent session evidence changed");
    SessionRepository sessions;
    const auto session = sessions.load(plan.context.conversation, *tx);
    if (!session) return session.error();
    if (!session.value() || session.value()->revision != plan.context.session_revision)
        return inconclusive("session evidence is unavailable or stale");
    const auto claim = m_claims.inspect(intent_id, now, *tx);
    if (!claim) return claim.error();
    recovery.claim = claim.value();
    if (!claim.value()) return inconclusive("claim evidence is unavailable");
    if (claim.value()->claim_token != plan.context.claim_token)
        return inconclusive("claim token no longer identifies the original attempt");
    const auto expected_claim_revision = next_storage_revision(plan.context.claim_row_revision);
    if (!expected_claim_revision)
        return inconclusive("claim revision evidence is exhausted");

    DesiredGenerationRepository generations;
    const auto marker = generations.load(plan.context.conversation.encode(), *tx);
    if (!marker) {
        if (marker.error().code == ErrorCode::StorageNotFound)
            return inconclusive("desired generation evidence is unavailable");
        return marker.error();
    }
    if (marker.value().value != plan.context.desired_generation
        || marker.value().row_revision != plan.context.desired_generation_row_revision) {
        return inconclusive("desired generation evidence is unavailable or stale");
    }
    AppliedTargetRepository applied;
    const auto target = applied.load(plan.context.target_identity, *tx);
    if (!target) return target.error();
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
