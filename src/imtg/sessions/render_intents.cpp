#include "imtg/sessions/render_intents.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <utility>

namespace imtg {

namespace {

constexpr std::string_view render_namespace = "render_intents";
constexpr std::string_view claim_namespace = "render_intent_claims";
constexpr std::string_view generation_namespace = "render_intent_generations";

Error claim_error(ErrorCode code, const char* message)
{
    return Error{code, message};
}

void append_u64(std::string& value, std::uint64_t number)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value.push_back(static_cast<char>((number >> shift) & 0xffU));
    }
}

void append_i64(std::string& value, std::int64_t number)
{
    append_u64(value, static_cast<std::uint64_t>(number));
}

void append_string(std::string& value, std::string_view text)
{
    append_u64(value, static_cast<std::uint64_t>(text.size()));
    value.append(text.data(), text.size());
}

class Reader {
public:
    explicit Reader(std::string_view value) : m_value(value) {}

    Result<std::uint64_t> u64()
    {
        if (remaining() < 8) return claim_error(ErrorCode::TruncatedEncoding, "claim envelope is truncated");
        std::uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            result |= static_cast<std::uint64_t>(static_cast<unsigned char>(m_value[m_offset++])) << shift;
        }
        return result;
    }

    Result<unsigned char> byte()
    {
        if (remaining() < 1) return claim_error(ErrorCode::TruncatedEncoding, "claim envelope is truncated");
        return static_cast<unsigned char>(m_value[m_offset++]);
    }

    Result<std::string> string()
    {
        const auto size = u64();
        if (!size) return size.error();
        if (size.value() > remaining() || size.value() > 64 * 1024) {
            return claim_error(ErrorCode::StorageRecordTooLarge, "claim envelope field is too large");
        }
        const auto begin = m_value.data() + m_offset;
        m_offset += static_cast<std::size_t>(size.value());
        return std::string(begin, static_cast<std::size_t>(size.value()));
    }

    std::size_t remaining() const noexcept { return m_value.size() - m_offset; }

private:
    std::string_view m_value;
    std::size_t m_offset = 0;
};

std::int64_t time_value(ClockTime value)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

ClockTime from_time_value(std::int64_t value)
{
    return ClockTime{std::chrono::milliseconds{value}};
}

Result<StorageBytes> encode_claim(const RenderIntentClaim& claim)
{
    if (claim.id.value.empty()) return claim_error(ErrorCode::InvalidArgument, "claim identity is empty");
    if (static_cast<unsigned>(claim.state) > static_cast<unsigned>(RenderIntentLifecycleState::Superseded)) {
        return claim_error(ErrorCode::InvalidArgument, "claim state is invalid");
    }
    if (claim.superseded_by && claim.superseded_by->value.empty()) {
        return claim_error(ErrorCode::InvalidArgument, "superseding identity is empty");
    }
    std::string value;
    value.reserve(64 + claim.superseded_by.value_or(RenderIntentId{}).value.size());
    value.append("v1", 2);
    value.push_back(static_cast<char>(claim.state));
    append_u64(value, claim.claim_token.value);
    append_i64(value, time_value(claim.lease_until));
    append_i64(value, time_value(claim.next_eligible_at));
    value.push_back(claim.superseded_by ? '\x01' : '\x00');
    if (claim.superseded_by) append_string(value, claim.superseded_by->value);
    return value;
}

Result<StorageBytes> encode_generation(std::uint64_t generation)
{
    if (generation == 0) return claim_error(ErrorCode::InvalidArgument, "desired generation is zero");
    std::string value{"v1", 2};
    append_u64(value, generation);
    return value;
}

Result<std::uint64_t> decode_generation(std::string_view encoded)
{
    if (encoded.size() != 10 || encoded.substr(0, 2) != "v1") {
        return claim_error(ErrorCode::StorageSerializationFailure, "desired generation marker is malformed");
    }
    Reader reader(encoded.substr(2));
    const auto generation = reader.u64();
    if (!generation || generation.value() == 0 || reader.remaining() != 0) {
        return claim_error(ErrorCode::StorageSerializationFailure, "desired generation marker is malformed");
    }
    return generation.value();
}

Result<RenderIntentClaim> decode_claim(const RenderIntentId& id, std::string_view encoded)
{
    if (encoded.size() < 3 || encoded.substr(0, 2) != "v1") {
        return claim_error(ErrorCode::UnsupportedVersion, "claim envelope version is unsupported");
    }
    Reader reader(encoded.substr(2));
    const auto state = reader.byte();
    if (!state || state.value() > static_cast<unsigned>(RenderIntentLifecycleState::Superseded)) {
        return claim_error(ErrorCode::StorageSerializationFailure, "claim state is malformed");
    }
    const auto token = reader.u64();
    const auto lease = reader.u64();
    const auto next = reader.u64();
    if (!token || !lease || !next) return claim_error(ErrorCode::StorageSerializationFailure, "claim envelope is malformed");
    const auto has_superseding_result = reader.byte();
    if (!has_superseding_result) return has_superseding_result.error();
    const auto has_superseding = has_superseding_result.value();
    if (has_superseding > 1) return claim_error(ErrorCode::StorageSerializationFailure, "claim marker is malformed");
    std::optional<RenderIntentId> superseded_by;
    if (has_superseding != 0) {
        const auto text = reader.string();
        if (!text) return text.error();
        superseded_by = RenderIntentId{text.value()};
    }
    if (reader.remaining() != 0) return claim_error(ErrorCode::StorageSerializationFailure, "claim envelope has trailing bytes");
    RenderIntentClaim result{id, static_cast<RenderIntentLifecycleState>(state.value()),
                             ClaimToken{token.value()}, from_time_value(static_cast<std::int64_t>(lease.value())),
                             from_time_value(static_cast<std::int64_t>(next.value())), 0, superseded_by};
    return result;
}

struct ClaimContext {
    RenderIntentClaim claim;
    StorageRevision base_revision{};
    bool has_side_state = false;
};

Result<std::optional<ClaimContext>> load_context(const RenderIntentId& id,
                                                 ClockTime fallback_time,
                                                 ITransaction& transaction)
{
    if (id.value.empty()) return claim_error(ErrorCode::InvalidArgument, "claim identity is empty");
    RenderIntentRepository intents;
    const auto base = transaction.read(render_namespace, id.value);
    if (!base) return base.error();
    if (!base.value()) return std::optional<ClaimContext>{};
    const auto decoded = intents.load(id, transaction);
    if (!decoded) return decoded.error();
    if (!decoded.value()) return std::optional<ClaimContext>{};
    const auto side = transaction.read(claim_namespace, id.value);
    if (!side) return side.error();
    if (!side.value()) {
        return std::optional<ClaimContext>{ClaimContext{
            RenderIntentClaim{id, RenderIntentLifecycleState::Pending, {}, {}, fallback_time, 0, {}},
            base.value()->revision, false}};
    }
    auto claim = decode_claim(id, side.value()->value);
    if (!claim) return claim.error();
    claim.value().row_revision = side.value()->revision;
    return std::optional<ClaimContext>{ClaimContext{
        std::move(claim).value(), base.value()->revision, true}};
}

Result<RenderIntentClaim> write_claim(ClaimContext context, RenderIntentClaim next,
                                      ITransaction& transaction)
{
    const auto watched = transaction.watch(render_namespace, context.claim.id.value,
                                           context.base_revision);
    if (!watched) return watched.error();
    const auto encoded = encode_claim(next);
    if (!encoded) return encoded.error();
    Result<StorageRevision> written = context.has_side_state
        ? transaction.write(claim_namespace, context.claim.id.value, encoded.value(), context.claim.row_revision)
        : transaction.create_if_absent(claim_namespace, context.claim.id.value, encoded.value());
    if (!written) return written.error();
    next.row_revision = written.value();
    return next;
}

bool lease_live(const RenderIntentClaim& claim, ClockTime now) noexcept
{
    return claim.state == RenderIntentLifecycleState::Claimed && now < claim.lease_until;
}

} // namespace

Result<DesiredGeneration> DesiredGenerationRepository::ensure(
    std::string_view scope, ITransaction& transaction) const
{
    if (scope.empty()) return claim_error(ErrorCode::InvalidArgument, "desired generation scope is empty");
    const auto found = transaction.read(generation_namespace, scope);
    if (!found) return found.error();
    if (found.value()) {
        const auto generation = decode_generation(found.value()->value);
        if (!generation) return generation.error();
        return DesiredGeneration{generation.value(), found.value()->revision};
    }
    const auto encoded = encode_generation(1);
    if (!encoded) return encoded.error();
    const auto created = transaction.create_if_absent(generation_namespace, scope, encoded.value());
    if (!created) return created.error();
    return DesiredGeneration{1, created.value()};
}

Result<DesiredGeneration> DesiredGenerationRepository::load(
    std::string_view scope, ITransaction& transaction) const
{
    if (scope.empty()) return claim_error(ErrorCode::InvalidArgument, "desired generation scope is empty");
    const auto found = transaction.read(generation_namespace, scope);
    if (!found) return found.error();
    if (!found.value()) return claim_error(ErrorCode::StorageNotFound, "desired generation marker is absent");
    const auto generation = decode_generation(found.value()->value);
    if (!generation) return generation.error();
    return DesiredGeneration{generation.value(), found.value()->revision};
}

Result<DesiredGeneration> DesiredGenerationRepository::advance(
    std::string_view scope, std::uint64_t expected_generation,
    StorageRevision expected_row_revision, ITransaction& transaction) const
{
    if (expected_generation == 0 || expected_row_revision == 0) {
        return claim_error(ErrorCode::InvalidArgument, "desired generation expectation is invalid");
    }
    const auto current = load(scope, transaction);
    if (!current) return current.error();
    if (current.value().value != expected_generation
        || current.value().row_revision != expected_row_revision) {
        return claim_error(ErrorCode::StorageConflict, "desired generation is stale");
    }
    const auto next = next_storage_revision(expected_generation);
    if (!next) return next.error();
    const auto encoded = encode_generation(next.value());
    if (!encoded) return encoded.error();
    const auto watched = transaction.watch(generation_namespace, scope, expected_row_revision);
    if (!watched) return watched.error();
    const auto written = transaction.write(generation_namespace, scope, encoded.value(), expected_row_revision);
    if (!written) return written.error();
    return DesiredGeneration{next.value(), written.value()};
}

Result<std::optional<RenderIntentClaim>> RenderIntentClaimRepository::inspect(
    const RenderIntentId& id, ClockTime fallback_time, ITransaction& transaction) const
{
    const auto context = load_context(id, fallback_time, transaction);
    if (!context) return context.error();
    if (!context.value()) return std::optional<RenderIntentClaim>{};
    return std::optional<RenderIntentClaim>{context.value()->claim};
}

Result<RenderIntentClaimPage> RenderIntentClaimRepository::claim_render_intents(
    std::size_t limit, ClockTime now, std::chrono::milliseconds lease_duration,
    std::string_view continuation, ITransaction& transaction) const
{
    if (limit == 0 || lease_duration.count() <= 0) {
        return claim_error(ErrorCode::InvalidArgument, "claim limit and lease must be positive");
    }
    RenderIntentClaimPage result;
    std::string cursor{continuation};
    while (result.claims.size() < limit) {
        const auto page = transaction.scan(render_namespace, cursor, limit);
        if (!page) return page.error();
        if (page.value().empty()) break;
        cursor = page.value().back().key;
        result.continuation = cursor;
        for (const auto& entry : page.value()) {
            const auto context = load_context(RenderIntentId{entry.key}, now, transaction);
            if (!context) return context.error();
            if (!context.value()) continue;
            auto current = context.value()->claim;
            if (current.state == RenderIntentLifecycleState::Completed
                || current.state == RenderIntentLifecycleState::Planned
                || current.state == RenderIntentLifecycleState::Superseded) continue;
            if (current.next_eligible_at > now
                || (current.state == RenderIntentLifecycleState::Claimed && now < current.lease_until)) continue;
            const auto next_token = next_storage_revision(current.claim_token.value);
            if (!next_token) return next_token.error();
            RenderIntentClaim next = current;
            next.state = RenderIntentLifecycleState::Claimed;
            next.claim_token = ClaimToken{next_token.value()};
            next.lease_until = now + lease_duration;
            next.next_eligible_at = now;
            auto written = write_claim(*context.value(), next, transaction);
            if (!written) return written.error();
            result.claims.push_back(std::move(written).value());
            if (result.claims.size() == limit) break;
        }
        if (page.value().size() < limit) break;
    }
    return result;
}

Result<RenderIntentClaim> RenderIntentClaimRepository::renew_render_intent(
    const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
    ClockTime now, ClockTime lease_until, ITransaction& transaction) const
{
    if (lease_until <= now) return claim_error(ErrorCode::InvalidArgument, "lease expiry must be in the future");
    const auto context = load_context(id, now, transaction);
    if (!context) return context.error();
    if (!context.value() || !context.value()->has_side_state) return claim_error(ErrorCode::StorageStaleClaim, "claim metadata is absent");
    if (context.value()->claim.row_revision != expected_row_revision
        || context.value()->claim.claim_token != token || !lease_live(context.value()->claim, now)) {
        return claim_error(ErrorCode::StorageStaleClaim, "claim token or lease is stale");
    }
    auto next = context.value()->claim;
    next.lease_until = lease_until;
    return write_claim(*context.value(), next, transaction);
}

Result<RenderIntentClaim> RenderIntentClaimRepository::complete_render_intent(
    const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
    ClockTime now, ITransaction& transaction) const
{
    const auto context = load_context(id, now, transaction);
    if (!context) return context.error();
    if (!context.value() || !context.value()->has_side_state) return claim_error(ErrorCode::StorageStaleClaim, "claim metadata is absent");
    if (context.value()->claim.row_revision != expected_row_revision
        || context.value()->claim.claim_token != token || !lease_live(context.value()->claim, now)) {
        return claim_error(ErrorCode::StorageStaleClaim, "claim token or lease is stale");
    }
    auto next = context.value()->claim;
    next.state = RenderIntentLifecycleState::Completed;
    next.lease_until = ClockTime{};
    next.next_eligible_at = now;
    return write_claim(*context.value(), next, transaction);
}

Result<RenderIntentClaim> RenderIntentClaimRepository::release_render_intent(
    const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
    ClockTime now, ClockTime next_eligible_at, ITransaction& transaction) const
{
    const auto context = load_context(id, now, transaction);
    if (!context) return context.error();
    if (!context.value() || !context.value()->has_side_state) return claim_error(ErrorCode::StorageStaleClaim, "claim metadata is absent");
    if (context.value()->claim.row_revision != expected_row_revision
        || context.value()->claim.claim_token != token || !lease_live(context.value()->claim, now)) {
        return claim_error(ErrorCode::StorageStaleClaim, "claim token or lease is stale");
    }
    auto next = context.value()->claim;
    next.state = RenderIntentLifecycleState::Pending;
    next.lease_until = ClockTime{};
    next.next_eligible_at = next_eligible_at;
    return write_claim(*context.value(), next, transaction);
}

Result<RenderIntentClaim> RenderIntentClaimRepository::supersede_render_intent(
    const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
    ClockTime now, const RenderIntentId& newer_intent_id, ITransaction& transaction) const
{
    if (newer_intent_id.value.empty()) return claim_error(ErrorCode::InvalidArgument, "newer intent identity is empty");
    const auto context = load_context(id, now, transaction);
    if (!context) return context.error();
    if (!context.value() || !context.value()->has_side_state) return claim_error(ErrorCode::StorageStaleClaim, "claim metadata is absent");
    if (context.value()->claim.row_revision != expected_row_revision
        || context.value()->claim.claim_token != token || !lease_live(context.value()->claim, now)) {
        return claim_error(ErrorCode::StorageStaleClaim, "claim token or lease is stale");
    }
    const auto newer = load_context(newer_intent_id, now, transaction);
    if (!newer) return newer.error();
    if (!newer.value()) return claim_error(ErrorCode::StorageNotFound, "newer intent is absent");
    auto next = context.value()->claim;
    next.state = RenderIntentLifecycleState::Superseded;
    next.lease_until = ClockTime{};
    next.superseded_by = newer_intent_id;
    return write_claim(*context.value(), next, transaction);
}

} // namespace imtg
