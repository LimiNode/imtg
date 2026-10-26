#include "imtg/runtime/reference_runtime.hpp"

#include <exception>
#include <algorithm>
#include <stdexcept>

namespace imtg {

namespace {
thread_local std::size_t g_error_sink_depth = 0;
}

Result<UiRevisions> FakeReconciler::reconcile(const ConversationKey& key,
                                              const UiTree& desired)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_applied.find(key);
    if (found == m_applied.end()) {
        AppliedTree entry{desired, UiRevisions{1, 1}};
        m_applied.emplace(key, entry);
        return entry.revisions;
    }
    if (found->second.tree == desired) {
        return found->second.revisions;
    }
    ++found->second.revisions.desired;
    found->second.revisions.applied = found->second.revisions.desired;
    found->second.tree = desired;
    return found->second.revisions;
}

UiRevisions FakeReconciler::revisions(const ConversationKey& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_applied.find(key);
    return found == m_applied.end() ? UiRevisions{} : found->second.revisions;
}

NotificationSubscription::NotificationSubscription(NotificationSubscription&& other) noexcept
    : m_state(std::move(other.m_state)), m_id(other.m_id)
{
    other.m_id = 0;
}

NotificationSubscription& NotificationSubscription::operator=(
    NotificationSubscription&& other) noexcept
{
    if (this != &other) {
        reset();
        m_state = std::move(other.m_state);
        m_id = other.m_id;
        other.m_id = 0;
    }
    return *this;
}

NotificationSubscription::~NotificationSubscription()
{
    reset();
}

void NotificationSubscription::reset() noexcept
{
    const auto state = m_state.lock();
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const auto& entry : state->entries) {
            if (entry->id == m_id) {
                entry->active = false;
                break;
            }
        }
        if (state->in_flight == 0) {
            state->entries.erase(std::remove_if(state->entries.begin(), state->entries.end(),
                                                [](const auto& entry) { return !entry->active; }),
            state->entries.end());
        }
    }
    m_state.reset();
    m_id = 0;
}

NotificationDispatcher::NotificationDispatcher(ErrorSink error_sink)
    : m_state(std::make_shared<NotificationState>())
{
    m_state->error_sink = std::move(error_sink);
}

NotificationDispatcher::~NotificationDispatcher()
{
    (void)close_and_wait();
}

Result<NotificationSubscription> NotificationDispatcher::subscribe(Callback callback)
{
    if (!callback) {
        return Error{ErrorCode::InvalidArgument, "notification callback is empty"};
    }
    std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->closed) {
        return Error{ErrorCode::InvalidState, "notification dispatcher is closed"};
    }
    const std::size_t id = ++m_state->next_id;
    m_state->entries.push_back(
        std::make_shared<NotificationEntry>(NotificationEntry{id, std::move(callback), true}));
    return NotificationSubscription(m_state, id);
}

void NotificationDispatcher::publish(const Notification& notification)
{
    std::vector<std::shared_ptr<NotificationEntry>> selected;
    std::exception_ptr selection_error;
    bool depth_exceeded = false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (m_state->closed) {
            return;
        }
        const auto thread_id = std::this_thread::get_id();
        const auto depth_entry = m_state->publication_depth.find(thread_id);
        const auto depth = depth_entry == m_state->publication_depth.end() ? 0 : depth_entry->second;
        if (depth >= m_state->max_callback_depth) {
            depth_exceeded = true;
        } else {
            ++m_state->publication_depth[thread_id];
            try {
                selected.reserve(m_state->entries.size());
            } catch (...) {
                selection_error = std::current_exception();
            }
            for (const auto& entry : m_state->entries) {
                if (selection_error || !entry->active) continue;
                try {
                    selected.push_back(entry);
                    ++m_state->in_flight;
                } catch (...) {
                    m_state->in_flight -= selected.size();
                    selection_error = std::current_exception();
                }
            }
        }
    }
    if (depth_exceeded) {
        selection_error = std::make_exception_ptr(
            std::runtime_error("notification callback depth exceeded"));
    }
    if (selection_error) {
        if (m_state->error_sink && g_error_sink_depth < m_state->max_callback_depth) {
            ++g_error_sink_depth;
            try { m_state->error_sink(selection_error); } catch (...) { }
            --g_error_sink_depth;
        }
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            auto found = m_state->publication_depth.find(std::this_thread::get_id());
            if (found != m_state->publication_depth.end() && --found->second == 0) {
                m_state->publication_depth.erase(found);
            }
        }
        return;
    }

    for (const auto& entry : selected) {
        std::exception_ptr bookkeeping_error;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            // Closing prevents new publications, but callbacks selected by this
            // publication are still completed before its quiescence boundary.
            if (!entry->active) {
                --m_state->in_flight;
                m_state->condition.notify_all();
                continue;
            }
            try {
                ++m_state->callback_depth[std::this_thread::get_id()];
            } catch (...) {
                --m_state->in_flight;
                bookkeeping_error = std::current_exception();
                m_state->condition.notify_all();
            }
        }
        if (bookkeeping_error) {
            if (m_state->error_sink && g_error_sink_depth < m_state->max_callback_depth) {
                ++g_error_sink_depth;
                try { m_state->error_sink(bookkeeping_error); } catch (...) { }
                --g_error_sink_depth;
            }
            continue;
        }
        std::exception_ptr callback_error;
        try {
            entry->callback(notification);
        } catch (...) {
            callback_error = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            auto depth = m_state->callback_depth.find(std::this_thread::get_id());
            if (depth != m_state->callback_depth.end() && --depth->second == 0) {
                m_state->callback_depth.erase(depth);
            }
            --m_state->in_flight;
            if (m_state->in_flight == 0) {
                m_state->entries.erase(std::remove_if(m_state->entries.begin(), m_state->entries.end(),
                                                      [](const auto& item) { return !item->active; }),
                                       m_state->entries.end());
            }
            m_state->condition.notify_all();
        }
        if (callback_error && m_state->error_sink
            && g_error_sink_depth < m_state->max_callback_depth) {
            ++g_error_sink_depth;
            try { m_state->error_sink(callback_error); } catch (...) { }
            --g_error_sink_depth;
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        auto found = m_state->publication_depth.find(std::this_thread::get_id());
        if (found != m_state->publication_depth.end() && --found->second == 0) {
            m_state->publication_depth.erase(found);
        }
    }
}

Result<void> NotificationDispatcher::close_and_wait()
{
    std::unique_lock<std::mutex> lock(m_state->mutex);
    m_state->closed = true;
    if (m_state->callback_depth.count(std::this_thread::get_id()) != 0
        || m_state->publication_depth.count(std::this_thread::get_id()) != 0) {
        return Error{ErrorCode::InvalidState, "cannot wait for dispatcher from callback"};
    }
    m_state->condition.wait(lock, [this] { return m_state->in_flight == 0; });
    m_state->entries.clear();
    return Result<void>();
}

ReferenceRuntime::ReferenceRuntime(ActionRegistry& actions,
                                   RenderFunction render,
                                   CommandFunction command,
                                   ErrorFunction error,
                                   RuntimeConfig config)
    : m_actions(actions), m_render(std::move(render)), m_command(std::move(command)),
      m_error(std::move(error)), m_config(config)
{
    if (m_config.worker_count == 0 || m_config.max_active_mailboxes == 0
        || m_config.max_queued_tasks_total == 0
        || m_config.max_queued_tasks_per_conversation == 0 || m_config.max_token_size == 0) {
        throw std::invalid_argument("runtime configuration limits must be non-zero");
    }
    m_workers.reserve(m_config.worker_count);
    try {
        for (std::size_t index = 0; index < m_config.worker_count; ++index) {
            if (index == m_config.fail_worker_start_after) {
                throw std::runtime_error("injected worker startup failure");
            }
            m_workers.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_accepting = false;
            m_shutdown_requested = true;
        }
        m_condition.notify_all();
        for (auto& worker : m_workers) {
            if (worker.joinable()) worker.join();
        }
        throw;
    }
}

ReferenceRuntime::ReferenceRuntime(ActionRegistry& actions,
                                   RenderFunction render,
                                   CommandFunction command,
                                   ErrorFunction error,
                                   std::size_t queue_capacity)
    : ReferenceRuntime(actions, std::move(render), std::move(command), std::move(error),
                       RuntimeConfig{RuntimeConfig::default_worker_count,
                                     RuntimeConfig::default_max_active_mailboxes,
                                     RuntimeConfig::default_max_queued_tasks_total,
                                     queue_capacity,
                                     RuntimeConfig::default_max_token_size})
{
}

ReferenceRuntime::~ReferenceRuntime()
{
    (void)shutdown();
}

Result<void> ReferenceRuntime::submit(ConversationKey key, std::string action_token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_accepting) {
        return Error{ErrorCode::InvalidState, "runtime is shutting down"};
    }
    if (action_token.size() > m_config.max_token_size) {
        return Error{ErrorCode::InvalidArgument, "action token exceeds configured limit"};
    }
    auto mailbox_it = m_mailboxes.end();
    bool inserted = false;
    bool queued = false;
    bool was_scheduled = false;
    try {
        auto insertion = m_mailboxes.try_emplace(key);
        mailbox_it = insertion.first;
        inserted = insertion.second;
        auto& mailbox = mailbox_it->second;
        if (mailbox.queue.size() >= m_config.max_queued_tasks_per_conversation
            || m_queued_tasks >= m_config.max_queued_tasks_total) {
            if (inserted && mailbox.queue.empty() && !mailbox.scheduled) m_mailboxes.erase(mailbox_it);
            return Error{ErrorCode::QueueFull, "session queue is full"};
        }
        if (inserted && m_mailboxes.size() > m_config.max_active_mailboxes) {
            m_mailboxes.erase(mailbox_it);
            return Error{ErrorCode::QueueFull, "active mailbox limit is full"};
        }
        mailbox.queue.push_back(Task{key, std::move(action_token)});
        ++m_queued_tasks;
        was_scheduled = mailbox.scheduled;
        if (!mailbox.scheduled) {
            mailbox.scheduled = true;
            m_ready.push_back(key);
            m_condition.notify_one();
        }
        queued = true;
    } catch (...) {
        if (mailbox_it != m_mailboxes.end()) {
            auto& mailbox = mailbox_it->second;
            if (queued || (!was_scheduled && !mailbox.queue.empty())) {
                if (!mailbox.queue.empty()) {
                    mailbox.queue.pop_back();
                    --m_queued_tasks;
                }
            }
            if (!was_scheduled) mailbox.scheduled = false;
            if (inserted && mailbox.queue.empty() && !mailbox.scheduled) {
                m_mailboxes.erase(mailbox_it);
            }
        }
        return Error{ErrorCode::QueueFull, "runtime admission allocation failed"};
    }
    return Result<void>();
}

void ReferenceRuntime::request_shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_accepting = false;
    m_shutdown_requested = true;
    m_condition.notify_all();
}

Result<void> ReferenceRuntime::shutdown()
{
    request_shutdown();
    if (is_worker_thread()) {
        return Error{ErrorCode::InvalidState, "cannot wait for runtime from worker"};
    }
    std::lock_guard<std::mutex> join_lock(m_join_mutex);
    if (m_joined) {
        return failure_status();
    }
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_joined = true;
    return failure_status();
}

Result<void> ReferenceRuntime::failure_status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_terminal_error ? Result<void>(*m_terminal_error) : Result<void>();
}

std::size_t ReferenceRuntime::discarded_task_count() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_discarded_tasks;
}

void ReferenceRuntime::worker_loop() noexcept
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this] { return !m_ready.empty() || m_shutdown_requested; });
            if (m_ready.empty()) {
                if (m_shutdown_requested) return;
                continue;
            }
            const auto key = m_ready.front();
            m_ready.pop_front();
            auto found = m_mailboxes.find(key);
            if (found == m_mailboxes.end() || found->second.queue.empty()) continue;
            task = std::move(found->second.queue.front());
            found->second.queue.pop_front();
            --m_queued_tasks;
        }
        const auto completed_key = task.key;
        execute(std::move(task));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto found = m_mailboxes.find(completed_key);
            if (found != m_mailboxes.end() && !found->second.queue.empty()) {
                try {
                    if (m_config.fail_ready_requeue) throw std::bad_alloc();
                    m_ready.push_back(completed_key);
                    m_condition.notify_one();
                } catch (...) {
                    m_discarded_tasks += found->second.queue.size();
                    m_queued_tasks -= found->second.queue.size();
                    m_mailboxes.erase(found);
                    m_terminal_error = Error{ErrorCode::RuntimeFailure,
                                             "runtime stopped after ready-queue failure"};
                    m_accepting = false;
                    m_shutdown_requested = true;
                    m_condition.notify_all();
                }
            } else if (found != m_mailboxes.end()) {
                m_mailboxes.erase(found);
            }
        }
    }
}

FakeReconciler& ReferenceRuntime::reconciler() noexcept
{
    return m_reconciler;
}

void ReferenceRuntime::execute(Task task) noexcept
{
    try {
        const auto result = m_actions.invoke(task.token,
            [this, &task](const ActionCommand& command) {
                if (!m_command) {
                    return Result<void>(Error{
                        ErrorCode::DispatchRejected, "reference runtime has no command handler"});
                }
                return m_command(task.key, command);
            });
        if (!result) {
            report(task.key, result.error());
            return;
        }
        if (m_render) {
            const auto tree = m_render(task.key);
            if (!tree) {
                report(task.key, tree.error());
                return;
            }
            const auto revisions = m_reconciler.reconcile(task.key, tree.value());
            if (!revisions) {
                report(task.key, revisions.error());
            }
        }
    } catch (const std::exception& exception) {
        report(task.key, Error{ErrorCode::HandlerUnavailable, exception.what()});
    } catch (...) {
        report(task.key, Error{ErrorCode::HandlerUnavailable, "handler threw an unknown exception"});
    }
}

void ReferenceRuntime::report(const ConversationKey& key, const Error& error) const noexcept
{
    if (m_error) {
        try {
            m_error(key, error);
        } catch (...) {
        }
    }
}

bool ReferenceRuntime::is_worker_thread() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& worker : m_workers) {
        if (worker.get_id() == std::this_thread::get_id()) return true;
    }
    return false;
}

} // namespace imtg
