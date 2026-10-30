#ifndef IMTG_SESSIONS_FSM_HPP
#define IMTG_SESSIONS_FSM_HPP

#include "imtg/core/result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace imtg {

/// \brief Versioned, stable identifier for an FSM state.
/// \note The identity is configuration data and is safe to persist; guards
/// and callbacks are never persisted.
struct FsmStateId {
    std::string name; ///< Stable state name within the owning FSM schema.
    std::uint32_t version{}; ///< Schema version for the state definition.

    /// \brief Compares state identities by name and schema version.
    friend bool operator==(const FsmStateId& left, const FsmStateId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Versioned identifier for an FSM event.
struct FsmEventId {
    std::string name; ///< Stable event name within the owning FSM schema.
    std::uint32_t version{}; ///< Schema version for the event definition.

    /// \brief Compares event identities by name and schema version.
    friend bool operator==(const FsmEventId& left, const FsmEventId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Allocation-free hash functor for state identities.
struct StateIdHash {
    /// \brief Hashes a state identity without allocation.
    /// \param value State identity to hash.
    /// \return Hash value suitable for unordered containers.
    std::size_t operator()(const FsmStateId& value) const noexcept;
};

/// \brief Trust domain of an event presented to the FSM.
/// \details Normal events are application input. Cancellation and Timeout
/// use reserved canonical identities and are validated independently.
enum class FsmEventKind {
    Normal,
    Cancellation,
    Timeout,
};

/// \brief Immutable event input evaluated by a deterministic FSM.
struct FsmEvent {
    FsmEventId id; ///< Versioned identity used for transition lookup.
    FsmEventKind kind = FsmEventKind::Normal; ///< Trust-domain classification.
    std::string payload; ///< Optional application payload; never interpreted by the graph.

    /// \brief Builds the canonical cancellation event (`cancel/1`).
    /// \return An event whose kind is `Cancellation` and whose identity is reserved.
    static FsmEvent cancellation();
    /// \brief Builds the canonical timeout event (`timeout/1`).
    /// \return An event whose kind is `Timeout` and whose identity is reserved.
    static FsmEvent timeout();
};

/// \brief Controls whether a successful processor operation emits a render intent.
enum class RenderPolicy { Never, OnChange, Always };

/// \brief Result of one pure FSM transition evaluation.
struct FsmStepResult {
    FsmStateId state; ///< Resulting state identity.
    bool changed = false; ///< Whether the destination differs from the input state.
};

/// \brief Deterministic, application-independent transition graph.
/// \details The graph owns no callbacks beyond process-local guards. It is
/// safe to evaluate concurrently after construction, but mutation should be
/// completed before calls to `step()`.
class Fsm final {
public:
    /// \brief Process-local predicate evaluated before a transition is accepted.
    /// \note Guards are not serializable and are not invoked by persistence recovery.
    using Guard = std::function<bool(const FsmStateId&, const FsmEvent&)>;

    /// \brief Registers a normal transition.
    /// \param from State from which the transition is evaluated.
    /// \param event Normal event identity accepted by the transition.
    /// \param to Destination state; it must be registered or is registered by this call.
    /// \param guard Optional process-local predicate evaluated before the transition.
    /// \return A duplicate or invalid identity is reported as a typed error.
    /// \note Guards are not persisted and must be safe for the caller's evaluation context.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to, Guard guard = {});
    /// \brief Registers a transition with an explicit event kind.
    /// \param from State from which the transition is evaluated.
    /// \param event Event identity accepted by the transition.
    /// \param kind Trust-domain classification that must match the incoming event.
    /// \param to Destination state.
    /// \param guard Optional process-local predicate evaluated before the transition.
    /// \return A duplicate or invalid identity is reported as a typed error.
    /// \note `cancel/1` and `timeout/1` are reserved for their matching kinds.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmEventKind kind,
        FsmStateId to, Guard guard = {});
    /// \brief Compatibility overload with the event kind after the target state.
    /// \param from State from which the transition is evaluated.
    /// \param event Event identity accepted by the transition.
    /// \param to Destination state.
    /// \param kind Trust-domain classification that must match the incoming event.
    /// \param guard Optional process-local predicate evaluated before the transition.
    /// \return A duplicate or invalid identity is reported as a typed error.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to,
        FsmEventKind kind, Guard guard = {});
    /// \brief Marks a state terminal; terminal states reject all events.
    /// \param state Registered state to mark terminal.
    /// \return Success when the state exists, otherwise a typed validation error.
    [[nodiscard]] Result<void> mark_terminal(FsmStateId state);
    /// \brief Validates that a state is registered in this graph.
    /// \param state State identity to validate.
    /// \return Success when registered, otherwise a typed validation error.
    [[nodiscard]] Result<void> validate_state(const FsmStateId& state) const;
    /// \brief Evaluates one event without changing the graph.
    /// \param current State in which the event is received.
    /// \param event Event to evaluate, including its trust-domain kind.
    /// \return The destination and whether the state changed.
    /// \note The graph and its guards must not be mutated concurrently with evaluation.
    [[nodiscard]] Result<FsmStepResult> step(
        const FsmStateId& current, const FsmEvent& event) const;

private:
    struct EventKey {
        FsmStateId state;
        FsmEventId event;
        FsmEventKind kind{FsmEventKind::Normal};

        friend bool operator==(const EventKey& left, const EventKey& right) noexcept
        {
            return left.state == right.state && left.event == right.event
                && left.kind == right.kind;
        }
    };
    struct EventKeyHash {
        std::size_t operator()(const EventKey& value) const noexcept;
    };
    struct Transition {
        FsmStateId to;
        Guard guard;
    };

    std::unordered_set<FsmStateId, StateIdHash> m_states;
    std::unordered_map<EventKey, Transition, EventKeyHash> m_transitions;
    std::unordered_set<FsmStateId, StateIdHash> m_terminal_states;
};

} // namespace imtg

namespace std {

/// \brief Standard-library hash support for `FsmStateId`.
template <>
struct hash<imtg::FsmStateId> {
    /// \brief Hashes a state identity using its typed fields.
    /// \param value State identity to hash.
    /// \return Hash value suitable for unordered containers.
    size_t operator()(const imtg::FsmStateId& value) const noexcept
    {
        return hash<string>{}(value.name) ^
            (hash<uint32_t>{}(value.version) + static_cast<size_t>(0x9e3779b9U) +
             (hash<string>{}(value.name) << 6) + (hash<string>{}(value.name) >> 2));
    }
};

/// \brief Standard-library hash support for `FsmEventId`.
template <>
struct hash<imtg::FsmEventId> {
    /// \brief Hashes an event identity using its typed fields.
    /// \param value Event identity to hash.
    /// \return Hash value suitable for unordered containers.
    size_t operator()(const imtg::FsmEventId& value) const noexcept
    {
        return hash<string>{}(value.name) ^
            (hash<uint32_t>{}(value.version) + static_cast<size_t>(0x9e3779b9U) +
             (hash<string>{}(value.name) << 6) + (hash<string>{}(value.name) >> 2));
    }
};

} // namespace std

#endif // IMTG_SESSIONS_FSM_HPP
