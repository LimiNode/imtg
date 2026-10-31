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

/// \brief Desired and successfully applied render generations for a conversation.
struct UiRevisions {
    std::uint64_t desired{}; ///< Latest desired render generation.
    std::uint64_t applied{}; ///< Latest successfully reconciled generation.
};

/// \brief Bounded-admission and deterministic fault configuration for `ReferenceRuntime`.
struct RuntimeConfig {
    static constexpr std::size_t default_worker_count = 4; ///< Default worker-pool size.
    static constexpr std::size_t default_max_active_mailboxes = 1024; ///< Default key bound.
    static constexpr std::size_t default_max_queued_tasks_total = 4096; ///< Global queue bound.
    static constexpr std::size_t default_max_queued_tasks_per_conversation = 32; ///< Per-key bound.
    static constexpr std::size_t default_max_token_size = 4096; ///< External token byte bound.

    std::size_t worker_count = default_worker_count; ///< Number of worker threads.
    std::size_t max_active_mailboxes = default_max_active_mailboxes; ///< Resident mailbox bound.
    std::size_t max_queued_tasks_total = default_max_queued_tasks_total; ///< Global task bound.
    std::size_t max_queued_tasks_per_conversation = default_max_queued_tasks_per_conversation; ///< Per-key task bound.
    std::size_t max_token_size = default_max_token_size; ///< Maximum accepted token bytes.
    // Test-only deterministic fault injection; defaults disable injection.
    std::size_t fail_worker_start_after = static_cast<std::size_t>(-1); ///< Test-only start failure.
    std::size_t fail_queue_insertion_on_submit = static_cast<std::size_t>(-1); ///< Test-only admission failure.
    bool fail_ready_requeue = false; ///< Test-only worker requeue failure.
};

/// \brief Telegram-free deterministic fake reconciler for reference-runtime tests.
class FakeReconciler {
public:
    /// \brief Records one desired tree and advances desired/applied revisions atomically.
    /// \param key Conversation being reconciled.
    /// \param desired Deterministic desired UI tree.
    /// \return Updated revisions or a reconciliation error.
    [[nodiscard]] Result<UiRevisions> reconcile(const ConversationKey& key,
                                                const UiTree& desired);
    /// \brief Returns current revisions for a conversation, or zeroes when absent.
    /// \param key Conversation to inspect.
    /// \return Desired and applied generations.
    [[nodiscard]] UiRevisions revisions(const ConversationKey& key) const;

private:
    struct AppliedTree {
        UiTree tree;
        UiRevisions revisions;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<ConversationKey, AppliedTree> m_applied;
};

/// \brief Value-owned process-local notification.
struct Notification {
    std::string type; ///< Application-defined notification route.
    std::string payload; ///< Application-defined notification bytes.
};

/// \brief Shared subscription entry used to coordinate removal and in-flight delivery.
struct NotificationEntry {
    std::size_t id{}; ///< Dispatcher-local subscription identifier.
    std::function<void(const Notification&)> callback; ///< Synchronous callback.
    bool active = true; ///< Whether new publications may select this entry.
};

/// \brief Shared dispatcher state retained safely by RAII subscriptions.
struct NotificationState {
    std::mutex mutex; ///< Guards every mutable state field below.
    std::condition_variable condition; ///< Signals quiescence and close progress.
    std::vector<std::shared_ptr<NotificationEntry>> entries; ///< Registered callbacks.
    std::size_t in_flight{}; ///< Selected callbacks not yet released.
    std::size_t next_id{}; ///< Next dispatcher-local identifier.
    bool closed = false; ///< Whether new subscriptions/publications are rejected.
    std::unordered_map<std::thread::id, std::size_t> callback_depth; ///< Reentrancy depth.
    std::unordered_map<std::thread::id, std::size_t> publication_depth; ///< Publish depth.
    std::size_t active_publications{}; ///< Publish calls retaining callback references.
    std::function<void(std::exception_ptr)> error_sink; ///< Callback exception observer.
    std::size_t max_callback_depth = 64; ///< Fail-stop recursion bound.
};

/// \brief RAII removal handle; reset stops new delivery but does not join in-flight work.
class NotificationSubscription {
public:
    NotificationSubscription() noexcept = default;
    NotificationSubscription(const NotificationSubscription&) = delete;
    NotificationSubscription& operator=(const NotificationSubscription&) = delete;
    /// \brief Transfers subscription-removal ownership.
    /// \param other Subscription whose ownership is transferred.
    NotificationSubscription(NotificationSubscription&& other) noexcept;
    /// \brief Transfers ownership after resetting this subscription.
    /// \param other Subscription whose ownership is transferred.
    /// \return This subscription after the transfer.
    NotificationSubscription& operator=(NotificationSubscription&& other) noexcept;
    ~NotificationSubscription();

    /// \brief Prevents future callback selection without waiting for in-flight work.
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
    /// \brief Synchronous notification callback.
    using Callback = std::function<void(const Notification&)>;
    /// \brief Observer for exceptions escaping callbacks.
    using ErrorSink = std::function<void(std::exception_ptr)>;

    /// \brief Creates an open dispatcher with an optional exception observer.
    /// \param error_sink Sink called for callback exceptions; it must not throw.
    explicit NotificationDispatcher(ErrorSink error_sink = {});
    NotificationDispatcher(const NotificationDispatcher&) = delete;
    NotificationDispatcher& operator=(const NotificationDispatcher&) = delete;
    ~NotificationDispatcher();

    /// \brief Registers a callback until its returned subscription is reset/destroyed.
    /// \param callback Synchronous callback; concurrent publications may invoke it concurrently.
    /// \return RAII subscription or an error when closed/invalid.
    [[nodiscard]] Result<NotificationSubscription> subscribe(Callback callback);
    /// \brief Synchronously publishes to callbacks selected at publication start.
    /// \param notification Value borrowed for the duration of callback delivery.
    /// \note Callback exceptions are reported to the configured error sink.
    void publish(const Notification& notification);
    /// \brief Closes admission and waits for callbacks and retained references to quiesce.
    /// \return Success, or `InvalidState` when called reentrantly from delivery.
    [[nodiscard]] Result<void> close_and_wait();

private:
    friend class NotificationSubscription;
    std::shared_ptr<NotificationState> m_state;
};

/// \brief Per-conversation reference runtime with bounded queues and controlled shutdown.
class ReferenceRuntime {
public:
    /// \brief Produces a deterministic desired UI tree for a conversation.
    using RenderFunction = std::function<Result<UiTree>(const ConversationKey&)>;
    /// \brief Applies a dispatched command within the serialized conversation task.
    using CommandFunction = std::function<Result<void>(
        const ConversationKey&, const ActionCommand&)>;
    /// \brief Observes task failures; implementations must not throw.
    using ErrorFunction = std::function<void(const ConversationKey&, const Error&)>;

    /// \brief Starts a bounded keyed worker pool borrowing its callbacks and registry.
    /// \param actions Registry that must outlive the runtime and worker shutdown.
    /// \param render Desired-tree callback.
    /// \param command Command callback.
    /// \param error Failure observer.
    /// \param config Worker, admission and fault-injection configuration.
    ReferenceRuntime(ActionRegistry& actions,
                     RenderFunction render,
                     CommandFunction command,
                     ErrorFunction error,
                     RuntimeConfig config = {});
    /// \brief Compatibility constructor configuring the per-conversation queue capacity.
    /// \param actions Registry that must outlive the runtime.
    /// \param render Desired-tree callback.
    /// \param command Command callback.
    /// \param error Failure observer.
    /// \param queue_capacity Maximum queued tasks accepted for each conversation.
    ReferenceRuntime(ActionRegistry& actions,
                     RenderFunction render,
                     CommandFunction command,
                     ErrorFunction error,
                     std::size_t queue_capacity);
    ReferenceRuntime(const ReferenceRuntime&) = delete;
    ReferenceRuntime& operator=(const ReferenceRuntime&) = delete;
    /// The owner must destroy the runtime from a non-worker thread after shutdown().
    ~ReferenceRuntime();

    /// \brief Submits one token for FIFO processing under its conversation key.
    /// \param key Conversation serialization key.
    /// \param action_token External action token, copied into bounded storage.
    /// \return Success or a validation, capacity, shutdown or runtime failure.
    [[nodiscard]] Result<void> submit(ConversationKey key, std::string action_token);
    /// \brief Stops admission and requests workers to drain accepted tasks.
    void request_shutdown() noexcept;
    /// \brief Requests shutdown, joins workers and reports fail-stop state.
    /// \return Success or the first observable runtime failure.
    /// \note Must be called from a non-worker thread.
    [[nodiscard]] Result<void> shutdown();
    /// \brief Returns the current fail-stop status without changing lifecycle state.
    /// \return Success while healthy, otherwise the recorded runtime failure.
    [[nodiscard]] Result<void> failure_status() const;
    /// \brief Returns tasks discarded after abnormal worker failure.
    /// \return Number of accepted tasks that could not be processed.
    [[nodiscard]] std::size_t discarded_task_count() const noexcept;
    /// \brief Returns the runtime-owned deterministic reconciler.
    /// \return Mutable reconciler reference.
    /// \note The returned reference is valid only while the runtime lives.
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
