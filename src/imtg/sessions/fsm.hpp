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
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const FsmStateId& left, const FsmStateId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Versioned identifier for an FSM event.
struct FsmEventId {
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const FsmEventId& left, const FsmEventId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

/// \brief Allocation-free hash functor for state identities.
struct StateIdHash {
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
    FsmEventId id;
    FsmEventKind kind = FsmEventKind::Normal;
    std::string payload;

    /// \brief Builds the canonical cancellation event (`cancel/1`).
    static FsmEvent cancellation();
    /// \brief Builds the canonical timeout event (`timeout/1`).
    static FsmEvent timeout();
};

/// \brief Controls whether a successful processor operation emits a render intent.
enum class RenderPolicy { Never, OnChange, Always };

/// \brief Result of one pure FSM transition evaluation.
struct FsmStepResult {
    FsmStateId state;
    bool changed = false;
};

/// \brief Deterministic, application-independent transition graph.
/// \details The graph owns no callbacks beyond process-local guards. It is
/// safe to evaluate concurrently after construction, but mutation should be
/// completed before calls to `step()`.
class Fsm final {
public:
    using Guard = std::function<bool(const FsmStateId&, const FsmEvent&)>;

    /// \brief Registers a normal transition.
    /// \return A duplicate or invalid identity is reported as a typed error.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to, Guard guard = {});
    /// \brief Registers a transition with an explicit event kind.
    /// \note `cancel/1` and `timeout/1` are reserved for their matching kinds.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmEventKind kind,
        FsmStateId to, Guard guard = {});
    /// \brief Compatibility overload with the event kind after the target state.
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to,
        FsmEventKind kind, Guard guard = {});
    /// \brief Marks a state terminal; terminal states reject all events.
    [[nodiscard]] Result<void> mark_terminal(FsmStateId state);
    /// \brief Validates that a state is registered in this graph.
    [[nodiscard]] Result<void> validate_state(const FsmStateId& state) const;
    /// \brief Evaluates one event without changing the graph.
    /// \return The destination and whether the state changed.
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

template <>
struct hash<imtg::FsmStateId> {
    size_t operator()(const imtg::FsmStateId& value) const noexcept
    {
        return hash<string>{}(value.name) ^
            (hash<uint32_t>{}(value.version) + static_cast<size_t>(0x9e3779b9U) +
             (hash<string>{}(value.name) << 6) + (hash<string>{}(value.name) >> 2));
    }
};

template <>
struct hash<imtg::FsmEventId> {
    size_t operator()(const imtg::FsmEventId& value) const noexcept
    {
        return hash<string>{}(value.name) ^
            (hash<uint32_t>{}(value.version) + static_cast<size_t>(0x9e3779b9U) +
             (hash<string>{}(value.name) << 6) + (hash<string>{}(value.name) >> 2));
    }
};

} // namespace std

#endif // IMTG_SESSIONS_FSM_HPP
