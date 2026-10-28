#include "imtg/sessions/fsm.hpp"

#include <utility>

namespace imtg {

namespace {

Error invalid(const char* message)
{
    return Error{ErrorCode::InvalidArgument, message};
}

bool valid(const FsmStateId& id)
{
    return !id.name.empty() && id.version != 0;
}

bool valid(const FsmEventId& id)
{
    return !id.name.empty() && id.version != 0;
}

} // namespace

FsmEvent FsmEvent::cancellation()
{
    return FsmEvent{FsmEventId{"cancel", 1}, FsmEventKind::Cancellation, {}};
}

FsmEvent FsmEvent::timeout()
{
    return FsmEvent{FsmEventId{"timeout", 1}, FsmEventKind::Timeout, {}};
}

std::size_t StateIdHash::operator()(const FsmStateId& value) const noexcept
{
    const auto name = std::hash<std::string>{}(value.name);
    return name ^ (std::hash<std::uint32_t>{}(value.version) +
                   static_cast<std::size_t>(0x9e3779b9U) + (name << 6) + (name >> 2));
}

std::size_t Fsm::EventKeyHash::operator()(const EventKey& value) const noexcept
{
    const auto state = StateIdHash{}(value.state);
    const auto event = std::hash<std::string>{}(value.event.name) ^
        (std::hash<std::uint32_t>{}(value.event.version) +
         static_cast<std::size_t>(0x9e3779b9U));
    return state ^ (event + static_cast<std::size_t>(0x9e3779b9U) + (state << 6) + (state >> 2));
}

Result<void> Fsm::add_transition(FsmStateId from, FsmEventId event,
                                 FsmStateId to, Guard guard)
{
    if (!valid(from) || !valid(to) || !valid(event)) {
        return invalid("FSM state and event identities must be non-empty and versioned");
    }
    const EventKey key{from, event};
    if (m_transitions.find(key) != m_transitions.end()) {
        return Error{ErrorCode::DuplicateId, "FSM transition is already registered"};
    }
    m_states.insert(from);
    m_states.insert(to);
    m_transitions.emplace(key, Transition{std::move(to), std::move(guard)});
    return Result<void>();
}

Result<void> Fsm::mark_terminal(FsmStateId state)
{
    if (!valid(state)) return invalid("FSM terminal state identity is invalid");
    m_states.insert(state);
    m_terminal_states.insert(std::move(state));
    return Result<void>();
}

Result<void> Fsm::validate_state(const FsmStateId& state) const
{
    if (!valid(state)) return invalid("FSM state identity is invalid");
    if (m_states.find(state) == m_states.end()) {
        return Error{ErrorCode::UnsupportedVersion, "FSM state is not registered"};
    }
    return Result<void>();
}

Result<FsmStepResult> Fsm::step(const FsmStateId& current, const FsmEvent& event) const
{
    const auto state_check = validate_state(current);
    if (!state_check) return state_check.error();
    if (!valid(event.id)) return invalid("FSM event identity is invalid");
    if (m_terminal_states.find(current) != m_terminal_states.end()) {
        return Error{ErrorCode::InvalidState, "FSM terminal state rejects events"};
    }
    const auto found = m_transitions.find(EventKey{current, event.id});
    if (found == m_transitions.end()) {
        return Error{ErrorCode::InvalidState, "FSM transition is not defined"};
    }
    if (found->second.guard) {
        try {
            if (!found->second.guard(current, event)) {
                return Error{ErrorCode::DispatchRejected, "FSM transition guard rejected event"};
            }
        } catch (...) {
            return Error{ErrorCode::RuntimeFailure, "FSM transition guard failed"};
        }
    }
    return FsmStepResult{found->second.to, !(found->second.to == current)};
}

} // namespace imtg
