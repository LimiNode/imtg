#ifndef IMTG_RUNTIME_REFERENCE_RUNTIME_HPP
#define IMTG_RUNTIME_REFERENCE_RUNTIME_HPP

#include "imtg/actions/registry.hpp"
#include "imtg/core/identifiers.hpp"
#include "imtg/ui/tree.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace imtg {

struct UiRevisions {
    std::uint64_t desired{};
    std::uint64_t applied{};
};

struct RuntimeConfig {
    static constexpr std::size_t default_worker_count = 4;
    static constexpr std::size_t default_max_active_mailboxes = 1024;
    static constexpr std::size_t default_max_queued_tasks_total = 4096;
    static constexpr std::size_t default_max_queued_tasks_per_conversation = 32;
    static constexpr std::size_t default_max_token_size = 4096;

    std::size_t worker_count = default_worker_count;
    std::size_t max_active_mailboxes = default_max_active_mailboxes;
    std::size_t max_queued_tasks_total = default_max_queued_tasks_total;
    std::size_t max_queued_tasks_per_conversation = default_max_queued_tasks_per_conversation;
    std::size_t max_token_size = default_max_token_size;
    // Test-only deterministic fault injection; defaults disable injection.
    std::size_t fail_worker_start_after = static_cast<std::size_t>(-1);
    std::size_t fail_queue_insertion_on_submit = static_cast<std::size_t>(-1);
    bool fail_ready_requeue = false;
};

/// \brief Telegram-free deterministic fake reconciler for reference-runtime tests.
class FakeReconciler {
public:
    [[nodiscard]] Result<UiRevisions> reconcile(const ConversationKey& key,
                                                const UiTree& desired);
    [[nodiscard]] UiRevisions revisions(const ConversationKey& key) const;

private:
    struct AppliedTree {
        UiTree tree;
        UiRevisions revisions;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<ConversationKey, AppliedTree> m_applied;
};

struct Notification {
    std::string type;
    std::string payload;
};

struct NotificationEntry {
    std::size_t id{};
    std::function<void(const Notification&)> callback;
    bool active = true;
};

struct NotificationState {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::shared_ptr<NotificationEntry>> entries;
    std::size_t in_flight{};
    std::size_t next_id{};
    bool closed = false;
    std::unordered_map<std::thread::id, std::size_t> callback_depth;
    std::unordered_map<std::thread::id, std::size_t> publication_depth;
    std::size_t active_publications{};
    std::function<void(std::exception_ptr)> error_sink;
    std::size_t max_callback_depth = 64;
};

/// \brief RAII removal handle; reset stops new delivery but does not join in-flight work.
class NotificationSubscription {
public:
    NotificationSubscription() noexcept = default;
    NotificationSubscription(const NotificationSubscription&) = delete;
    NotificationSubscription& operator=(const NotificationSubscription&) = delete;
    NotificationSubscription(NotificationSubscription&& other) noexcept;
    NotificationSubscription& operator=(NotificationSubscription&& other) noexcept;
    ~NotificationSubscription();

    void reset() noexcept;

private:
    friend class NotificationDispatcher;
    NotificationSubscription(std::weak_ptr<NotificationState> state, std::size_t id)
        : m_state(std::move(state)), m_id(id)
    {
    }

    std::weak_ptr<NotificationState> m_state;
    std::size_t m_id{};
};

/// \brief Small explicit notification dispatcher with observable quiescence.
class NotificationDispatcher {
public:
    using Callback = std::function<void(const Notification&)>;
    using ErrorSink = std::function<void(std::exception_ptr)>;

    explicit NotificationDispatcher(ErrorSink error_sink = {});
    NotificationDispatcher(const NotificationDispatcher&) = delete;
    NotificationDispatcher& operator=(const NotificationDispatcher&) = delete;
    ~NotificationDispatcher();

    [[nodiscard]] Result<NotificationSubscription> subscribe(Callback callback);
    void publish(const Notification& notification);
    [[nodiscard]] Result<void> close_and_wait();

private:
    friend class NotificationSubscription;
    std::shared_ptr<NotificationState> m_state;
};

/// \brief Per-conversation reference runtime with bounded queues and controlled shutdown.
class ReferenceRuntime {
public:
    using RenderFunction = std::function<Result<UiTree>(const ConversationKey&)>;
    using CommandFunction = std::function<Result<void>(
        const ConversationKey&, const ActionCommand&)>;
    using ErrorFunction = std::function<void(const ConversationKey&, const Error&)>;

    ReferenceRuntime(ActionRegistry& actions,
                     RenderFunction render,
                     CommandFunction command,
                     ErrorFunction error,
                     RuntimeConfig config = {});
    ReferenceRuntime(ActionRegistry& actions,
                     RenderFunction render,
                     CommandFunction command,
                     ErrorFunction error,
                     std::size_t queue_capacity);
    ReferenceRuntime(const ReferenceRuntime&) = delete;
    ReferenceRuntime& operator=(const ReferenceRuntime&) = delete;
    /// The owner must destroy the runtime from a non-worker thread after shutdown().
    ~ReferenceRuntime();

    [[nodiscard]] Result<void> submit(ConversationKey key, std::string action_token);
    void request_shutdown() noexcept;
    [[nodiscard]] Result<void> shutdown();
    [[nodiscard]] Result<void> failure_status() const;
    [[nodiscard]] std::size_t discarded_task_count() const noexcept;
    [[nodiscard]] FakeReconciler& reconciler() noexcept;

private:
    struct Task {
        ConversationKey key;
        std::string token;
    };
    struct Mailbox {
        std::deque<Task> queue;
        bool scheduled = false;
    };

    void execute(Task task) noexcept;
    void report(const ConversationKey& key, const Error& error) const noexcept;
    [[nodiscard]] bool is_worker_thread() const noexcept;
    void worker_loop() noexcept;

    ActionRegistry& m_actions;
    RenderFunction m_render;
    CommandFunction m_command;
    ErrorFunction m_error;
    const RuntimeConfig m_config;
    mutable std::mutex m_mutex;
    std::unordered_map<ConversationKey, Mailbox> m_mailboxes;
    std::deque<ConversationKey> m_ready;
    std::vector<std::thread> m_workers;
    std::mutex m_join_mutex;
    bool m_joined = false;
    std::condition_variable m_condition;
    bool m_accepting = true;
    bool m_shutdown_requested = false;
    std::size_t m_queued_tasks = 0;
    std::size_t m_submit_attempts = 0;
    bool m_failed = false;
    ErrorCode m_failure_code = ErrorCode::InvalidState;
    std::size_t m_discarded_tasks = 0;
    FakeReconciler m_reconciler;
};

} // namespace imtg

#endif // IMTG_RUNTIME_REFERENCE_RUNTIME_HPP
