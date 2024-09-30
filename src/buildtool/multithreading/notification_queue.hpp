// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>  // std::forward

#include "gsl/gsl"
#include "src/buildtool/multithreading/task.hpp"
#include "src/utils/cpp/atomic.hpp"

// Counter that can block the caller until it reaches zero.
class WaitableZeroCounter {
  public:
    explicit WaitableZeroCounter(std::size_t init = 0) : count_{init} {}

    void Decrement() {
        std::shared_lock lock{mutex_};
        if (--count_ == 0) {
            cv_.notify_all();
        }
    }

    void Increment() { ++count_; }

    void WaitForZero() {
        while (not IsZero()) {  // loop to protect against spurious wakeups
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [this] { return IsZero(); });
        }
    }

    void Abort() {
        std::shared_lock lock{mutex_};
        done_ = true;
        cv_.notify_all();
    }

  private:
    std::shared_mutex mutex_;
    std::condition_variable_any cv_;
    std::atomic<std::size_t> count_;
    std::atomic<bool> done_;

    [[nodiscard]] auto IsZero() noexcept -> bool {
        return count_ == 0 or done_;
    }
};

class NotificationQueue {
  public:
    explicit NotificationQueue(
        gsl::not_null<WaitableZeroCounter*> const& total_workload)
        : total_workload_{total_workload} {}

    NotificationQueue(NotificationQueue const& other) = delete;
    NotificationQueue(NotificationQueue&& other) noexcept
        : queue_{std::move(other.queue_)},
          done_{other.done_},
          total_workload_{other.total_workload_} {}
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
            return not queue_.empty() or done_;
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
        if (not lock or queue_.empty()) {
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
        total_workload_->Increment();
        {
            std::unique_lock lock{mutex_};
            queue_.emplace_back(std::forward<FunctionType>(f));
        }
        ready_.notify_one();
    }

    // Returns false if mutex is locked without pushing the task, pushes task
    // and returns true otherwise
    template <typename FunctionType>
    [[nodiscard]] auto try_push(FunctionType&& f) -> bool {
        {
            std::unique_lock lock{mutex_, std::try_to_lock};
            if (not lock) {
                return false;
            }
            total_workload_->Increment();
            queue_.emplace_back(std::forward<FunctionType>(f));
        }
        ready_.notify_one();
        return true;
    }

    // Method to communicate to the notification queue that there will not be
    // any more queries. Queries after calling this method are not guarantied to
    // work as expected
    void done() {
        {
            std::unique_lock lock{mutex_};
            done_ = true;
        }
        ready_.notify_all();
    }

  private:
    std::deque<Task> queue_;
    bool done_{false};
    std::mutex mutex_;
    std::condition_variable ready_;
    gsl::not_null<WaitableZeroCounter*> total_workload_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_NOTIFICATION_QUEUE_HPP
