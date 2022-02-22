#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_SYSTEM_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_SYSTEM_HPP

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include "src/buildtool/multithreading/notification_queue.hpp"

class TaskSystem {
  public:
    // Constructors create as many threads as specified (or
    // std::thread::hardware_concurrency() many if not specified) running
    // `TaskSystem::Run(index)` on them, where `index` is their position in
    // `threads_`
    TaskSystem();
    explicit TaskSystem(std::size_t number_of_threads);

    TaskSystem(TaskSystem const&) = delete;
    TaskSystem(TaskSystem&&) = delete;
    auto operator=(TaskSystem const&) -> TaskSystem& = delete;
    auto operator=(TaskSystem &&) -> TaskSystem& = delete;

    // Destructor calls sets to "done" all notification queues and joins the
    // threads. Note that joining the threads will wait until the Run method
    // they are running is finished
    ~TaskSystem();

    // Queue a task. Task will be added to the first notification queue that is
    // found to be unlocked or, if none is found (after kNumberOfAttemps
    // iterations), to the one in `index+1` position waiting until it's
    // unlocked.
    template <typename FunctionType>
    void QueueTask(FunctionType&& f) noexcept {
        auto idx = index_++;

        for (std::size_t i = 0; i < thread_count_ * kNumberOfAttempts; ++i) {
            if (queues_[(idx + i) % thread_count_].try_push(
                    std::forward<FunctionType>(f))) {
                return;
            }
        }
        queues_[idx % thread_count_].push(std::forward<FunctionType>(f));
    }

    [[nodiscard]] auto NumberOfThreads() const noexcept -> std::size_t {
        return thread_count_;
    }

  private:
    std::size_t const thread_count_{
        std::max(1U, std::thread::hardware_concurrency())};
    std::vector<std::thread> threads_{};
    std::vector<NotificationQueue> queues_{};
    std::atomic<std::size_t> index_{0};
    WaitableOneWayFlag queues_read_{};
    WaitableZeroCounter num_threads_running_{};

    static constexpr std::size_t kNumberOfAttempts = 5;

    void Run(std::size_t idx);
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_SYSTEM_HPP
