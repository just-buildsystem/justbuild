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

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_NODE_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_NODE_HPP

#include <atomic>
#include <mutex>
#include <optional>
#include <utility>  // std::move
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/multithreading/task.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/gsl.hpp"

// Wrapper around Value to enable async access to it in a continuation-style
// programming way
template <typename Key, typename Value>
class AsyncMapNode {
  public:
    explicit AsyncMapNode(Key key) : key_{std::move(key)} {}

    /// \brief Set value and queue awaiting tasks to the task system under a
    /// unique lock. Awaiting tasks are cleared to ensure node does not hold
    /// (shared) ownership of any data related to the task once they are given
    /// to the task system
    /// \param[in]  ts      task system to which tasks will be queued
    /// \param[in]  value   value to set
    void SetAndQueueAwaitingTasks(gsl::not_null<TaskSystem*> const& ts,
                                  Value&& value) {
        std::unique_lock lock{m_};
        if (failed_) {
            // The node is failed already; no value can be set.
            return;
        }
        value_ = std::move(value);
        for (auto& task : awaiting_tasks_) {
            ts->QueueTask(std::move(task));
        }
        // After tasks are queued we need to release them and any other
        // information we are keeping about the tasks
        awaiting_tasks_.clear();
        failure_tasks_.clear();
    }

    /// \brief If node is not marked as queued to be processed, task is queued
    /// to the task system. A task to process the node (that is, set its value)
    /// can only be queued once. Lock free
    /// \param[in]  ts      task system
    /// \param[in]  task    processing task. Function type must have
    /// operator()()
    template <typename Function>
    void QueueOnceProcessingTask(gsl::not_null<TaskSystem*> const& ts,
                                 Function&& task) {
        // if node was already queued to be processed, nothing to do
        if (GetAndMarkQueuedToBeProcessed()) {
            return;
        }
        ts->QueueTask(std::forward<Function>(task));
    }

    /// \brief Ensure task will be queued to the task system once the value of
    /// the node is ready. This operation is lock free once the value is ready
    /// before that node is uniquely locked while task is being added to
    /// awaiting tasks
    /// \param[in]  ts      task system
    /// \param[in]  task    task awaiting for value. Function type must have
    /// operator()()
    /// \returns boolean indicating whether task was immediately queued.
    template <typename Function>
    [[nodiscard]] auto AddOrQueueAwaitingTask(
        gsl::not_null<TaskSystem*> const& ts,
        Function&& task) -> bool {
        if (IsReady()) {
            ts->QueueTask(std::forward<Function>(task));
            return true;
        }
        {
            std::unique_lock ul{m_};
            if (failed_) {
                // If the node is failed (and hence will never get ready), do
                // not queue any more tasks.
                return false;
            }
            // Check again in case the node was made ready after the lock-free
            // check by another thread
            if (IsReady()) {
                ts->QueueTask(std::forward<Function>(task));
                return true;
            }
            awaiting_tasks_.emplace_back(std::forward<Function>(task));
            return false;
        }
    }

    /// \brief Ensure task will be queued to the task system once the value of
    /// the node is ready. This operation is lock free once the value is ready
    /// before that node is uniquely locked while task is being added to
    /// awaiting tasks
    /// \param[in]  ts      task system
    /// \param[in]  task    task awaiting for value. Function type must have
    /// operator()()
    template <typename Function>
    void QueueOnFailure(gsl::not_null<TaskSystem*> const& ts, Function&& task) {
        if (IsReady()) {
            // The node is ready, so it won't fail any more.
            return;
        }
        {
            std::unique_lock ul{m_};
            if (failed_) {
                ts->QueueTask(std::forward<Function>(task));
            }
            else {
                failure_tasks_.emplace_back(std::forward<Function>(task));
            }
        }
    }

    /// \brief Mark the node as failed and schedule the cleanup tasks.
    /// \param[in]  ts      task system
    void Fail(gsl::not_null<TaskSystem*> const& ts) {
        std::unique_lock ul{m_};
        if (IsReady()) {
            // The node has a value already, so it can't be marked as failed any
            // more
            return;
        }
        if (failed_) {
            // The was already marked as failed and the failure handled.
            // So there is nothing more to do.
            return;
        }
        failed_ = true;
        // As the node will never become ready, we have to clean up all tasks
        // and schedule the failure tasks.
        for (auto& task : failure_tasks_) {
            ts->QueueTask(std::move(task));
        }
        awaiting_tasks_.clear();
        failure_tasks_.clear();
    }

    // Not thread safe, do not use unless the value has been already set
    [[nodiscard]] auto GetValue() const& noexcept -> Value const& {
        // Will only be checked in debug build
        ExpectsAudit(value_.has_value());
        return *value_;
    }
    [[nodiscard]] auto GetValue() && noexcept = delete;

    [[nodiscard]] auto GetKey() const& noexcept -> Key const& { return key_; }
    [[nodiscard]] auto GetKey() && noexcept -> Key { return std::move(key_); }

    [[nodiscard]] auto IsReady() const noexcept -> bool {
        return value_.has_value();
    }

  private:
    Key key_;
    std::optional<Value> value_;
    std::vector<Task> awaiting_tasks_;
    std::vector<Task> failure_tasks_;
    std::mutex m_;
    std::atomic<bool> is_queued_to_be_processed_{false};
    bool failed_{false};

    /// \brief Sets node as queued to be processed
    /// \returns True if it was already queued to be processed, false
    /// otherwise
    /// Note: this is an atomic, lock-free operation
    [[nodiscard]] auto GetAndMarkQueuedToBeProcessed() noexcept -> bool {
        return std::atomic_exchange(&is_queued_to_be_processed_, true);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_NODE_HPP
