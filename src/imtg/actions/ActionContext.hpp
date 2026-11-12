#ifndef IMTG_ACTIONS_ACTION_CONTEXT_HPP
#define IMTG_ACTIONS_ACTION_CONTEXT_HPP

/// \file imtg/actions/ActionContext.hpp
/// \brief Standalone public component; no preceding umbrella include is required.

#include "ActionCommand.hpp"
#include <imtg/core.hpp>

#include <functional>
#include <utility>

namespace imtg {

class ActionRegistry;

/// \brief Invocation-scoped command capability; it cannot be used after invoke returns.
class ActionContext {
public:
    /// \brief Synchronous sink that accepts commands during one invocation.
    using DispatchSink = std::function<Result<void>(const ActionCommand&)>;

    /// \brief Creates an invocation-scoped context around a command sink.
    /// \param sink Sink called synchronously by `dispatch()`.
    explicit ActionContext(DispatchSink sink) : m_sink(std::move(sink)) {}
    ActionContext(const ActionContext&) = delete;
    ActionContext& operator=(const ActionContext&) = delete;

    /// \brief Dispatches one command while the invocation remains active.
    /// \param command Command transferred to the configured sink.
    /// \return Sink result or `DispatchRejected` after the invocation ends.
    [[nodiscard]] Result<void> dispatch(ActionCommand command)
    {
        if (!m_active) {
            return Error{ErrorCode::DispatchRejected, "action context is no longer active"};
        }
        if (!m_sink) {
            return Error{ErrorCode::DispatchRejected, "action context has no dispatch sink"};
        }
        return m_sink(command);
    }

private:
    friend class ActionRegistry;

    void deactivate() noexcept
    {
        m_active = false;
    }

    DispatchSink m_sink;
    bool m_active = true;
};

} // namespace imtg

#endif // IMTG_ACTIONS_ACTION_CONTEXT_HPP
