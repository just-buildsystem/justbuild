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

#include "src/buildtool/multithreading/task_system.hpp"

#include <optional>

#include "gsl/gsl"
#include "src/buildtool/multithreading/task.hpp"

TaskSystem::TaskSystem() : TaskSystem(std::thread::hardware_concurrency()) {}

TaskSystem::TaskSystem(std::size_t number_of_threads)
    : thread_count_{std::max(std::size_t{1}, number_of_threads)},
      total_workload_{thread_count_} {
    for (std::size_t index = 0; index < thread_count_; ++index) {
        queues_.emplace_back(&total_workload_);
    }
    for (std::size_t index = 0; index < thread_count_; ++index) {
        threads_.emplace_back([&, index]() { Run(index); });
    }
}

TaskSystem::~TaskSystem() {
    Finish();    // wait for tasks to finish
    Shutdown();  // stop the threads
    for (auto& t : threads_) {
        t.join();
    }
}

void TaskSystem::Shutdown() noexcept {
    shutdown_ = true;
    // Abort the workload counter in case a system shut down was requested while
    // the task queues (workload) are not yet empty and someone is still waiting
    // on this counter to become zero.
    total_workload_.Abort();
    for (auto& q : queues_) {
        q.done();
    }
}

void TaskSystem::Finish() noexcept {
    // When starting a new task system all spawned threads will immediately go
    // to sleep and wait for tasks. Even after adding some tasks, it can take a
    // while until the first thread wakes up. Therefore, we need to wait for the
    // total workload (number of active threads _and_ total number of queued
    // tasks) to become zero.
    total_workload_.WaitForZero();
}

void TaskSystem::Run(std::size_t idx) {
    Expects(thread_count_ > 0);

    while (not shutdown_) {
        std::optional<Task> t{};
        for (std::size_t i = 0; i < thread_count_; ++i) {
            t = queues_[(idx + i) % thread_count_].try_pop();
            if (t) {
                break;
            }
        }

        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        t = t ? t : queues_[idx % thread_count_].pop();

        if (not t or shutdown_) {
            break;
        }

        (*t)();
    }
}
