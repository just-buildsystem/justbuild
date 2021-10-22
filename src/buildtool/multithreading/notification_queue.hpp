#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>  // std::forward

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/multithreading/task.hpp"
#include "src/utils/cpp/atomic.hpp"

// Counter that can block the caller until it reaches zero.
class WaitableZeroCounter {
  public:
    explicit WaitableZeroCounter(std::size_t init = 0) : count_{init} {}

    void Decrement() {
        if (--count_ == 0) {
            count_.notify_all();
        }
    }

    void Increment() { ++count_; }

    void WaitForZero() {
        auto val = count_.load();
        while (val != 0) {  // loop to protect against spurious wakeups
            count_.wait(val);
            val = count_.load();
        }
    }

  private:
    atomic<std::size_t> count_{};
};

class NotificationQueue {
  public:
    explicit NotificationQueue(
        gsl::not_null<WaitableZeroCounter*> total_workload)
        : total_workload_{std::move(total_workload)} {}

    NotificationQueue(NotificationQueue const& other) = delete;
    NotificationQueue(NotificationQueue&& other) noexcept
        : queue_{std::move(other.queue_)},
          done_{other.done_},
          total_workload_{std::move(other.total_workload_)} {}
    ~NotificationQueue() = default;

    [[nodiscard]] auto operator=(NotificationQueue const& other)
        -> NotificationQueue& = delete;
    [[nodiscard]] auto operator=(NotificationQueue&& other)
        -> NotificationQueue& = delete;

    // Blocks the thread until it's possible to pop or we are done.
    // Note that the lock releases ownership of the mutex while waiting
    // for the queue to have some element or for the notification queue
    // state to be set to "done".
    // Returns task popped or nullopt if no task was popped
    [[nodiscard]] auto pop() -> std::optional<Task> {
        std::unique_lock lock{mutex_};
        auto there_is_something_to_pop_or_we_are_done = [&]() {
            return !queue_.empty() || done_;
        };
        if (not there_is_something_to_pop_or_we_are_done()) {
            total_workload_->Decrement();
            ready_.wait(lock, there_is_something_to_pop_or_we_are_done);
            total_workload_->Increment();
        }

        if (queue_.empty()) {
            return std::nullopt;
        }
        auto t = std::move(queue_.front());
        queue_.pop_front();
        total_workload_->Decrement();
        return t;
    }

    // Returns nullopt if the mutex is already locked or the queue is empty,
    // otherwise pops the front element of the queue and returns it
    [[nodiscard]] auto try_pop() -> std::optional<Task> {
        std::unique_lock lock{mutex_, std::try_to_lock};
        if (!lock || queue_.empty()) {
            return std::nullopt;
        }
        auto t = std::move(queue_.front());
        queue_.pop_front();
        total_workload_->Decrement();
        return t;
    }

    // Push task once the mutex is available (locking it until addition is
    // finished)
    template <typename FunctionType>
    void push(FunctionType&& f) {
        {
            std::unique_lock lock{mutex_};
            queue_.emplace_back(std::forward<FunctionType>(f));
        }
        total_workload_->Increment();
        ready_.notify_one();
    }

    // Returns false if mutex is locked without pushing the task, pushes task
    // and returns true otherwise
    template <typename FunctionType>
    [[nodiscard]] auto try_push(FunctionType&& f) -> bool {
        {
            std::unique_lock lock{mutex_, std::try_to_lock};
            if (!lock) {
                return false;
            }
            queue_.emplace_back(std::forward<FunctionType>(f));
        }
        total_workload_->Increment();
        ready_.notify_one();
        return true;
    }

    // Method to communicate to the notification queue that there will not be
    // any more queries. Queries after calling this method are not guaratied to
    // work as expected
    void done() {
        {
            std::unique_lock lock{mutex_};
            done_ = true;
        }
        ready_.notify_all();
    }

  private:
    std::deque<Task> queue_{};
    bool done_{false};
    std::mutex mutex_{};
    std::condition_variable ready_{};
    gsl::not_null<WaitableZeroCounter*> total_workload_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP
