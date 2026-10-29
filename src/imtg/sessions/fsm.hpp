#ifndef IMTG_SESSIONS_FSM_HPP
#define IMTG_SESSIONS_FSM_HPP

#include "imtg/core/result.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace imtg {

struct FsmStateId {
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const FsmStateId& left, const FsmStateId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

struct FsmEventId {
    std::string name;
    std::uint32_t version{};

    friend bool operator==(const FsmEventId& left, const FsmEventId& right) noexcept
    {
        return left.version == right.version && left.name == right.name;
    }
};

struct StateIdHash {
    std::size_t operator()(const FsmStateId& value) const noexcept;
};

enum class FsmEventKind {
    Normal,
    Cancellation,
    Timeout,
};

struct FsmEvent {
    FsmEventId id;
    FsmEventKind kind = FsmEventKind::Normal;
    std::string payload;

    static FsmEvent cancellation();
    static FsmEvent timeout();
};

enum class RenderPolicy { Never, OnChange, Always };

struct FsmStepResult {
    FsmStateId state;
    bool changed = false;
};

/// Deterministic, application-independent transition graph.
class Fsm final {
public:
    using Guard = std::function<bool(const FsmStateId&, const FsmEvent&)>;

    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to, Guard guard = {});
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmEventKind kind,
        FsmStateId to, Guard guard = {});
    [[nodiscard]] Result<void> add_transition(
        FsmStateId from, FsmEventId event, FsmStateId to,
        FsmEventKind kind, Guard guard = {});
    [[nodiscard]] Result<void> mark_terminal(FsmStateId state);
    [[nodiscard]] Result<void> validate_state(const FsmStateId& state) const;
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
