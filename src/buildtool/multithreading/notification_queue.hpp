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

// Flag that can block the caller until it is set. Cannot be cleared after set.
class WaitableOneWayFlag {
  public:
    // Clear flag. Essentially a noop, if it was set before.
    void Clear() {
        if (not was_set_) {
            initial_ = false;
        }
    }

    // Set flag. Essentially a noop, if it was set before.
    void Set() {
        if (not was_set_) {
            was_set_ = true;
            was_set_.notify_all();
        }
    }

    // Blocks caller until it is set, if it was ever cleared.
    void WaitForSet() {
        if (not was_set_ and not initial_) {
            was_set_.wait(false);
        }
    }

  private:
    atomic<bool> was_set_{};
    bool initial_{true};
};

// Counter that can block the caller until it reaches zero.
class WaitableZeroCounter {
    enum class Status { Init, Wait, Reached };

  public:
    explicit WaitableZeroCounter(std::size_t init = 0) : count_{init} {}

    // Essentially a noop, if count reached zero since last wait call.
    void Decrement() {
        if (status_ != Status::Reached and --count_ == 0) {
            if (status_ == Status::Wait) {
                status_ = Status::Reached;
                status_.notify_all();
            }
        }
    }

    // Essentially a noop, if count reached zero since last wait call.
    void Increment() {
        if (status_ != Status::Reached) {
            ++count_;
        }
    }

    // Blocks caller until count reached zero, since last call to this method.
    void WaitForZero() {
        status_ = Status::Wait;
        if (count_ != 0) {
            status_.wait(Status::Wait);
        }
        status_ = Status::Reached;
    }

  private:
    std::atomic<std::size_t> count_{};
    atomic<Status> status_{Status::Init};
};

class NotificationQueue {
  public:
    NotificationQueue(gsl::not_null<WaitableOneWayFlag*> queues_read,
                      gsl::not_null<WaitableZeroCounter*> num_threads_running)
        : queues_read_{std::move(queues_read)},
          num_threads_running_{std::move(num_threads_running)} {}

    NotificationQueue(NotificationQueue const& other) = delete;
    NotificationQueue(NotificationQueue&& other) noexcept
        : queue_{std::move(other.queue_)},
          done_{other.done_},
          queues_read_{std::move(other.queues_read_)},
          num_threads_running_{std::move(other.num_threads_running_)} {}
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
            num_threads_running_->Decrement();
            ready_.wait(lock, there_is_something_to_pop_or_we_are_done);
            num_threads_running_->Increment();
        }

        if (queue_.empty()) {
            return std::nullopt;
        }
        auto t = std::move(queue_.front());
        queue_.pop_front();
        queues_read_->Set();
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
        queues_read_->Set();
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
        queues_read_->Clear();
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
        queues_read_->Clear();
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
    gsl::not_null<WaitableOneWayFlag*> queues_read_;
    gsl::not_null<WaitableZeroCounter*> num_threads_running_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP
