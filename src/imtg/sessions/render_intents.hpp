#ifndef IMTG_SESSIONS_RENDER_INTENTS_HPP
#define IMTG_SESSIONS_RENDER_INTENTS_HPP

#include "imtg/core/clock.hpp"
#include "imtg/sessions/session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace imtg {

/// \brief Monotonic fencing token for one render-intent claim row.
struct ClaimToken {
    std::uint64_t value{}; ///< Persisted token value; zero means never claimed.

    /// \brief Compares two fencing tokens.
    /// \param left First token.
    /// \param right Second token.
    /// \return `true` when both token values match.
    friend bool operator==(ClaimToken left, ClaimToken right) noexcept
    {
        return left.value == right.value;
    }
    /// \brief Checks whether two fencing tokens differ.
    /// \param left First token.
    /// \param right Second token.
    /// \return `true` when token values differ.
    friend bool operator!=(ClaimToken left, ClaimToken right) noexcept
    {
        return !(left == right);
    }
};

/// \brief Durable lifecycle state for an intent's reconciliation ownership.
enum class RenderIntentLifecycleState { Pending, Claimed, Planned, Completed, Superseded };

/// \brief Additive claim and lease metadata for one legacy-compatible intent.
struct RenderIntentClaim {
    RenderIntentId id; ///< Stable identity of the base render intent.
    RenderIntentLifecycleState state = RenderIntentLifecycleState::Pending; ///< Durable lifecycle state.
    ClaimToken claim_token; ///< Latest fencing token assigned to this row.
    ClockTime lease_until{}; ///< Lease expiry in the authoritative clock domain.
    ClockTime next_eligible_at{}; ///< Earliest time at which a claim may be attempted.
    StorageRevision row_revision{}; ///< Side-state storage revision observed by the caller.
    std::optional<RenderIntentId> superseded_by; ///< Newer intent that superseded this row, if any.
};

/// \brief Bounded result of a claim scan.
struct RenderIntentClaimPage {
    std::vector<RenderIntentClaim> claims; ///< Claims staged by this transaction.
    std::optional<std::string> continuation; ///< Exclusive key for the next bounded scan page.
};

/// \brief Monotonic desired-state generation used to fence reconciliation plans.
struct DesiredGeneration {
    std::uint64_t value{}; ///< Generation value; it never decreases.
    StorageRevision row_revision{}; ///< Storage revision observed for the marker row.
};

/// \brief Additive per-conversation desired-generation marker repository.
/// \details Initialization and advancement are compare-and-set operations in
/// the caller-owned transaction. A concurrent initializer therefore loses at
/// commit and must inspect the committed marker before retrying.
class DesiredGenerationRepository final {
public:
    /// \brief Atomically initializes or reads a conversation's marker.
    /// \param scope Stable conversation identity used as the marker key.
    /// \param transaction Caller-owned read-write transaction for initialization.
    /// \return Existing marker or candidate generation one.
    [[nodiscard]] Result<DesiredGeneration> ensure(
        std::string_view scope, ITransaction& transaction) const;

    /// \brief Loads a marker without creating it.
    /// \param scope Stable conversation identity used as the marker key.
    /// \param transaction Caller-owned transaction.
    /// \return Marker or `StorageNotFound` when uninitialized.
    [[nodiscard]] Result<DesiredGeneration> load(
        std::string_view scope, ITransaction& transaction) const;

    /// \brief Advances a marker using generation and row-revision fencing.
    /// \param scope Stable conversation identity used as the marker key.
    /// \param expected_generation Generation observed by the caller.
    /// \param expected_row_revision Storage revision observed by the caller.
    /// \param transaction Caller-owned read-write transaction.
    /// \return The next monotonic generation staged for commit.
    [[nodiscard]] Result<DesiredGeneration> advance(
        std::string_view scope, std::uint64_t expected_generation,
        StorageRevision expected_row_revision, ITransaction& transaction) const;
};

/// \brief Backend-neutral claim, lease and fencing repository for render intents.
/// \details Claim metadata is stored in an additive side namespace, preserving
/// the original M2b render-intent bytes and identities. Methods stage changes in
/// the caller-owned transaction; a claim is authoritative only after that
/// transaction commits successfully.
class RenderIntentClaimRepository final {
public:
    /// \brief Reads one claim envelope, treating a valid legacy row as Pending.
    /// \param id Stable render-intent identity.
    /// \param fallback_time Eligibility time used for an unmaterialized legacy row.
    /// \param transaction Caller-owned transaction.
    /// \return Claim metadata, or empty when the base intent is absent.
    [[nodiscard]] Result<std::optional<RenderIntentClaim>> inspect(
        const RenderIntentId& id, ClockTime fallback_time, ITransaction& transaction) const;

    /// \brief Claims a bounded page of eligible intents with monotonic fencing.
    /// \param limit Non-zero maximum number of eligible records to claim.
    /// \param now Current authoritative time.
    /// \param lease_duration Positive lease duration.
    /// \param continuation Exclusive stable base-key cursor; empty starts at the first key.
    /// \param transaction Caller-owned read-write transaction.
    /// \return Up to `limit` claims staged in this transaction and a continuation cursor.
    /// \note A successful return is tentative until the caller commits. A
    /// commit result of `Unknown` requires `inspect()` before another claim.
    [[nodiscard]] Result<RenderIntentClaimPage> claim_render_intents(
        std::size_t limit, ClockTime now, std::chrono::milliseconds lease_duration,
        std::string_view continuation, ITransaction& transaction) const;

    /// \brief Renews a live claim without changing its fencing token.
    /// \param id Stable intent identity.
    /// \param token Token returned by a committed claim.
    /// \param expected_row_revision Side-state revision returned by that claim.
    /// \param now Current authoritative time; an expired lease is stale.
    /// \param lease_until New strictly-future lease expiry.
    /// \param transaction Caller-owned read-write transaction.
    /// \return The renewed claim staged for commit.
    [[nodiscard]] Result<RenderIntentClaim> renew_render_intent(
        const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
        ClockTime now, ClockTime lease_until, ITransaction& transaction) const;

    /// \brief Completes a live claim using token and row-revision fencing.
    /// \param id Stable intent identity.
    /// \param token Token returned by a committed claim.
    /// \param expected_row_revision Side-state revision returned by that claim.
    /// \param now Current authoritative time; an expired lease is stale.
    /// \param transaction Caller-owned read-write transaction.
    /// \return The terminal Completed state staged for commit.
    [[nodiscard]] Result<RenderIntentClaim> complete_render_intent(
        const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
        ClockTime now, ITransaction& transaction) const;

    /// \brief Releases a live claim back to Pending at a selected eligible time.
    /// \param id Stable intent identity.
    /// \param token Token returned by a committed claim.
    /// \param expected_row_revision Side-state revision returned by that claim.
    /// \param now Current authoritative time; an expired lease is stale.
    /// \param next_eligible_at Earliest time at which the released row may be claimed.
    /// \param transaction Caller-owned read-write transaction.
    /// \return The Pending state staged for commit.
    [[nodiscard]] Result<RenderIntentClaim> release_render_intent(
        const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
        ClockTime now, ClockTime next_eligible_at, ITransaction& transaction) const;

    /// \brief Marks a live claim superseded by a newer intent.
    /// \param id Stable intent identity.
    /// \param token Token returned by a committed claim.
    /// \param expected_row_revision Side-state revision returned by that claim.
    /// \param now Current authoritative time; an expired lease is stale.
    /// \param newer_intent_id Stable newer intent identity retained for audit.
    /// \param transaction Caller-owned read-write transaction.
    /// \return The terminal Superseded state staged for commit.
    [[nodiscard]] Result<RenderIntentClaim> supersede_render_intent(
        const RenderIntentId& id, ClaimToken token, StorageRevision expected_row_revision,
        ClockTime now, const RenderIntentId& newer_intent_id, ITransaction& transaction) const;
};

} // namespace imtg

#endif // IMTG_SESSIONS_RENDER_INTENTS_HPP
