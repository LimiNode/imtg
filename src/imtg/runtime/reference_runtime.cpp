#include <imtg/runtime.hpp>

#include <logit.hpp>

#include <exception>
#include <algorithm>
#include <stdexcept>

namespace imtg {

namespace {
thread_local std::size_t g_error_sink_depth = 0;

// A detached callback may re-enter the dispatcher while its captures unwind.
// Count that ownership until destruction completes, without allocating in reset().
class NotificationRetirement {
public:
    explicit NotificationRetirement(NotificationState& state) noexcept
        : m_state(state), m_previous(m_current)
    {
        m_current = this;
    }
    NotificationRetirement(const NotificationRetirement&) = delete;
    NotificationRetirement& operator=(const NotificationRetirement&) = delete;

    ~NotificationRetirement()
    {
        m_current = m_previous;
        std::lock_guard<std::mutex> lock(m_state.mutex);
        --m_state.active_retirements;
        m_state.condition.notify_all();
    }

    static bool contains(const NotificationState& state) noexcept
    {
        for (auto* current = m_current; current != nullptr; current = current->m_previous) {
            if (&current->m_state == &state) return true;
        }
        return false;
    }

private:
    NotificationState& m_state;
    NotificationRetirement* m_previous;
    static thread_local NotificationRetirement* m_current;
};

thread_local NotificationRetirement* NotificationRetirement::m_current = nullptr;
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
    std::shared_ptr<NotificationEntry> retired;
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = std::find_if(state->entries.begin(), state->entries.end(),
            [this](const auto& entry) { return entry->id == m_id; });
        if (found != state->entries.end()) {
            (*found)->active = false;
            retired = std::move(*found);
            state->entries.erase(found);
            ++state->active_retirements;
        }
    }
    m_state.reset();
    m_id = 0;
    if (retired) {
        NotificationRetirement retirement(*state);
        retired.reset();
    }
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
    auto entry = std::make_shared<NotificationEntry>(
        NotificationEntry{0, std::move(callback), true});
    std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->closed) {
        return Error{ErrorCode::InvalidState, "notification dispatcher is closed"};
    }
    const std::size_t id = ++m_state->next_id;
    entry->id = id;
    m_state->entries.push_back(entry);
    // Accepted ownership must not outlive the lock in this local variable.
    entry.reset();
    return NotificationSubscription(m_state, id);
}

void NotificationDispatcher::publish(const Notification& notification)
{
    std::vector<std::shared_ptr<NotificationEntry>> selected;
    std::exception_ptr selection_error;
    bool depth_exceeded = false;
    bool publication_registered = false;
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
            ++m_state->active_publications;
            publication_registered = true;
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
    const auto finish_publication = [&]() {
        if (!publication_registered) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_state->mutex);
        auto found = m_state->publication_depth.find(std::this_thread::get_id());
        if (found != m_state->publication_depth.end() && --found->second == 0) {
            m_state->publication_depth.erase(found);
        }
        --m_state->active_publications;
        publication_registered = false;
        m_state->condition.notify_all();
    };
    if (selection_error) {
        if (m_state->error_sink && g_error_sink_depth < m_state->max_callback_depth) {
            ++g_error_sink_depth;
            try { m_state->error_sink(selection_error); } catch (...) { }
            --g_error_sink_depth;
        }
        selected.clear();
        finish_publication();
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
            m_state->condition.notify_all();
        }
        if (callback_error && m_state->error_sink
            && g_error_sink_depth < m_state->max_callback_depth) {
            ++g_error_sink_depth;
            try { m_state->error_sink(callback_error); } catch (...) { }
            --g_error_sink_depth;
        }
    }
    selected.clear();
    finish_publication();
}

Result<void> NotificationDispatcher::close_and_wait()
{
    std::vector<std::shared_ptr<NotificationEntry>> retired;
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        m_state->closed = true;
        if (m_state->callback_depth.count(std::this_thread::get_id()) != 0
            || m_state->publication_depth.count(std::this_thread::get_id()) != 0
            || NotificationRetirement::contains(*m_state)) {
            return Error{ErrorCode::InvalidState, "cannot wait for dispatcher from callback"};
        }
        m_state->condition.wait(lock, [this] {
            return m_state->in_flight == 0 && m_state->active_publications == 0
                && m_state->active_retirements == 0;
        });
        retired.swap(m_state->entries);
        if (!retired.empty()) ++m_state->active_retirements;
    }
    if (!retired.empty()) {
        NotificationRetirement retirement(*m_state);
        retired.clear();
    }
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
        try { LOGIT_ERROR("runtime.startup_failed"); } catch (...) { }
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
                                     RuntimeConfig::default_max_token_size,
                                     static_cast<std::size_t>(-1),
                                     static_cast<std::size_t>(-1),
                                     false})
{
}

ReferenceRuntime::~ReferenceRuntime()
{
    (void)shutdown();
}

Result<void> ReferenceRuntime::submit(ConversationKey key, std::string action_token)
{
    return submit_task(Task{std::move(key), std::move(action_token), {}});
}

Result<void> ReferenceRuntime::submit_callback(ConversationKey key, CallbackTask callback)
{
    if (!callback) {
        return Error{ErrorCode::InvalidArgument, "runtime callback task is empty"};
    }
    return submit_task(Task{std::move(key), {}, std::move(callback)});
}

Result<void> ReferenceRuntime::submit_task(Task task)
{
    const auto result = [&]() -> Result<void> {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto task_key = task.key;
        if (!m_accepting) {
            return Error{ErrorCode::InvalidState, "runtime is shutting down"};
        }
        if (task.token.size() > m_config.max_token_size) {
            return Error{ErrorCode::InvalidArgument, "action token exceeds configured limit"};
        }
        auto mailbox_it = m_mailboxes.end();
        bool inserted = false;
        bool enqueued = false;
        bool was_scheduled = false;
        const auto submit_index = m_submit_attempts++;
        try {
            auto insertion = m_mailboxes.try_emplace(task_key);
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
            was_scheduled = mailbox.scheduled;
            if (submit_index == m_config.fail_queue_insertion_on_submit) {
                throw std::bad_alloc();
            }
            mailbox.queue.push_back(std::move(task));
            ++m_queued_tasks;
            enqueued = true;
            if (!mailbox.scheduled) {
                mailbox.scheduled = true;
                m_ready.push_back(task_key);
                m_condition.notify_one();
            }
        } catch (...) {
            if (mailbox_it != m_mailboxes.end()) {
                auto& mailbox = mailbox_it->second;
                if (enqueued) {
                    if (!mailbox.queue.empty()) {
                        mailbox.queue.pop_back();
                        --m_queued_tasks;
                    }
                }
                // A failed insertion never changes scheduling. If a later mutation
                // fails, only undo the scheduling mutation performed by this call.
                if (enqueued && !was_scheduled && mailbox.queue.empty()) {
                    mailbox.scheduled = false;
                    if (!m_ready.empty() && m_ready.back() == task_key) {
                        m_ready.pop_back();
                    }
                }
                if (inserted && mailbox.queue.empty() && !mailbox.scheduled) {
                    m_mailboxes.erase(mailbox_it);
                }
            }
            return Error{ErrorCode::QueueFull, "runtime admission allocation failed"};
        }
        return Result<void>();
    }();
    // Backend code runs only after admission has released the runtime lock.
    if (!result) {
        try {
            LOGIT_WARN_TAG("runtime.admission_rejected",
                LOGIT_TAG("error_code", static_cast<int>(result.error().code)));
        } catch (...) { }
    }
    return result;
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
        try { LOGIT_WARN("runtime.shutdown_rejected"); } catch (...) { }
        return Error{ErrorCode::InvalidState, "cannot wait for runtime from worker"};
    }
    std::unique_lock<std::mutex> join_lock(m_join_mutex);
    if (m_joined) {
        return failure_status();
    }
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_joined = true;
    const auto result = failure_status();
    join_lock.unlock();
    try {
        if (result) {
            LOGIT_INFO("runtime.shutdown");
        } else {
            LOGIT_ERROR("runtime.shutdown_failed");
        }
    } catch (...) { }
    return result;
}

Result<void> ReferenceRuntime::failure_status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_failed
        ? Result<void>(Error{m_failure_code, "runtime stopped after an internal failure"})
        : Result<void>();
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
        bool fail_stop = false;
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
                    m_failed = true;
                    m_failure_code = ErrorCode::RuntimeFailure;
                    m_accepting = false;
                    m_shutdown_requested = true;
                    m_condition.notify_all();
                    fail_stop = true;
                }
            } else if (found != m_mailboxes.end()) {
                m_mailboxes.erase(found);
            }
        }
        if (fail_stop) {
            try { LOGIT_ERROR("runtime.fail_stop"); } catch (...) { }
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
        if (task.callback) {
            const auto result = task.callback();
            if (!result) {
                report(task.key, result.error());
            }
            return;
        }
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
        try {
            report(task.key, Error{ErrorCode::HandlerUnavailable, exception.what()});
        } catch (...) {
        }
    } catch (...) {
        try {
            report(task.key, Error{ErrorCode::HandlerUnavailable,
                                   "handler threw an unknown exception"});
        } catch (...) {
        }
    }
}

void ReferenceRuntime::report(const ConversationKey& key, const Error& error) const noexcept
{
    try {
        LOGIT_ERROR_TAG("runtime.task_failed",
            LOGIT_TAG("error_code", static_cast<int>(error.code)));
    } catch (...) { }
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
