#include "src/buildtool/multithreading/task_system.hpp"

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/multithreading/task.hpp"

TaskSystem::TaskSystem() : TaskSystem(std::thread::hardware_concurrency()) {}

TaskSystem::TaskSystem(std::size_t number_of_threads)
    : thread_count_{std::max(1UL, number_of_threads)},
      total_workload_{thread_count_} {
    for (std::size_t index = 0; index < thread_count_; ++index) {
        queues_.emplace_back(&total_workload_);
    }
    for (std::size_t index = 0; index < thread_count_; ++index) {
        threads_.emplace_back([&, index]() { Run(index); });
    }
}

TaskSystem::~TaskSystem() {
    // When starting a new task system all spawned threads will immediately go
    // to sleep and wait for tasks. Even after adding some tasks, it can take a
    // while until the first thread wakes up. Therefore, we need to wait for the
    // total workload (number of active threads _and_ total number of queued
    // tasks) to become zero.
    total_workload_.WaitForZero();
    for (auto& q : queues_) {
        q.done();
    }
    for (auto& t : threads_) {
        t.join();
    }
}

void TaskSystem::Run(std::size_t idx) {
    gsl_Expects(thread_count_ > 0);

    while (true) {
        std::optional<Task> t{};
        for (std::size_t i = 0; i < thread_count_; ++i) {
            t = queues_[(idx + i) % thread_count_].try_pop();
            if (t) {
                break;
            }
        }

        // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
        t = t ? t : queues_[idx % thread_count_].pop();

        if (!t) {
            break;
        }

        (*t)();
    }
}
