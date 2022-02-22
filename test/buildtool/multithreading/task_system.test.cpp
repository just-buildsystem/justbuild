#include <chrono>
#include <mutex>
#include <numeric>  // std::iota
#include <string>
#include <thread>
#include <unordered_set>

#include "catch2/catch.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "test/utils/container_matchers.hpp"

namespace {

enum class CallStatus { kNotExecuted, kExecuted };

}  // namespace

TEST_CASE("Basic", "[task_system]") {
    SECTION("Empty task system terminates") {
        { TaskSystem ts; }
        CHECK(true);
    }
    SECTION("0-arguments constructor") {
        TaskSystem ts;
        CHECK(ts.NumberOfThreads() == std::thread::hardware_concurrency());
    }
    SECTION("1-argument constructor") {
        std::size_t const desired_number_of_threads_in_ts =
            GENERATE(1u, 2u, 5u, 10u, std::thread::hardware_concurrency());
        TaskSystem ts(desired_number_of_threads_in_ts);
        CHECK(ts.NumberOfThreads() == desired_number_of_threads_in_ts);
    }
}

TEST_CASE("Side effects of tasks are reflected out of ts", "[task_system]") {
    SECTION("Lambda function") {
        auto status = CallStatus::kNotExecuted;
        {  // Make sure that all tasks will be completed before the checks
            TaskSystem ts;
            ts.QueueTask([&status]() { status = CallStatus::kExecuted; });
        }
        CHECK(status == CallStatus::kExecuted);
    }
    SECTION("std::function") {
        auto status = CallStatus::kNotExecuted;
        {
            TaskSystem ts;
            std::function<void()> f{
                [&status]() { status = CallStatus::kExecuted; }};
            ts.QueueTask(f);
        }
        CHECK(status == CallStatus::kExecuted);
    }
    SECTION("Struct") {
        auto s = CallStatus::kNotExecuted;
        struct Callable {
            explicit Callable(CallStatus* cs) : status{cs} {}
            void operator()() const { *status = CallStatus::kExecuted; }
            CallStatus* status;
        };
        Callable c{&s};
        {
            TaskSystem ts;
            ts.QueueTask(c);
        }
        CHECK(&s == c.status);
        CHECK(s == CallStatus::kExecuted);
    }
    SECTION("Lambda capturing `this` inside struct") {
        std::string ext_name{};
        struct Wrapper {
            TaskSystem ts{};
            std::string name{};

            explicit Wrapper(std::string n) : name{std::move(n)} {}

            void QueueSetAndCheck(std::string* ext) {
                ts.QueueTask([this, ext]() {
                    SetDefaultName();
                    CheckDefaultName(ext);
                });
            }

            void SetDefaultName() { name = "Default"; }

            void CheckDefaultName(std::string* ext) const {
                *ext = name;
                CHECK(name == "Default");
            }
        };
        {
            Wrapper w{"Non-default name"};
            w.QueueSetAndCheck(&ext_name);
        }
        CHECK(ext_name == "Default");
    }
}

TEST_CASE("All tasks are executed", "[task_system]") {
    std::size_t const number_of_tasks = 1000;
    std::vector<int> tasks_executed;
    std::vector<int> queued_tasks(number_of_tasks);
    std::iota(std::begin(queued_tasks), std::end(queued_tasks), 0);
    std::mutex m;

    {
        TaskSystem ts;
        for (auto task_num : queued_tasks) {
            ts.QueueTask([&tasks_executed, &m, task_num]() {
                std::unique_lock l{m};
                tasks_executed.push_back(task_num);
            });
        }
    }

    CHECK_THAT(tasks_executed,
               HasSameElementsAs<std::vector<int>>(queued_tasks));
}

TEST_CASE("Task is executed even if it needs to wait for a long while",
          "[task_system]") {
    auto status = CallStatus::kNotExecuted;

    // Calculate what would take for the task system to be constructed, queue a
    // non-sleeping task, execute it and be destructed
    auto const start_no_sleep = std::chrono::high_resolution_clock::now();
    {
        TaskSystem ts;
        ts.QueueTask([&status]() { status = CallStatus::kExecuted; });
    }
    auto const end_no_sleep = std::chrono::high_resolution_clock::now();

    status = CallStatus::kNotExecuted;

    std::chrono::nanoseconds const sleep_time =
        10 * std::chrono::duration_cast<std::chrono::nanoseconds>(
                 end_no_sleep - start_no_sleep);
    auto const start = std::chrono::high_resolution_clock::now();
    {
        TaskSystem ts;
        ts.QueueTask([&status, sleep_time]() {
            std::this_thread::sleep_for(sleep_time);
            status = CallStatus::kExecuted;
        });
    }
    auto const end = std::chrono::high_resolution_clock::now();
    CHECK(end - start > sleep_time);
    CHECK(status == CallStatus::kExecuted);
}

TEST_CASE("All threads run until work is done", "[task_system]") {
    using namespace std::chrono_literals;
    static auto const kNumThreads = std::thread::hardware_concurrency();
    static auto const kFailTimeout = 10s;

    std::mutex mutex{};
    std::condition_variable cv{};
    std::unordered_set<std::thread::id> tids{};

    // Add thread id to set and wait for others to do the same.
    auto store_id = [&tids, &mutex, &cv]() -> void {
        std::unique_lock lock(mutex);
        tids.emplace(std::this_thread::get_id());
        cv.notify_all();
        cv.wait_for(
            lock, kFailTimeout, [&tids] { return tids.size() == kNumThreads; });
    };

    SECTION("single task produces multiple tasks") {
        {
            TaskSystem ts{kNumThreads};
            // Wait some time for all threads to go to sleep.
            std::this_thread::sleep_for(1s);

            // Run singe task that creates the actual store tasks. All threads
            // should stay alive until their corresponding queue is filled.
            ts.QueueTask([&ts, &store_id] {
                // One task per thread (assumes round-robin push to queues).
                for (std::size_t i{}; i < ts.NumberOfThreads(); ++i) {
                    ts.QueueTask([&store_id] { store_id(); });
                }
            });
        }
        CHECK(tids.size() == kNumThreads);
    }

    SECTION("multiple tasks reduce to one, which produces multiple tasks") {
        atomic<std::size_t> counter{};

        // All threads wait for counter, last thread creates 'store_id' tasks.
        auto barrier = [&counter, &store_id](TaskSystem* ts) {
            auto value = ++counter;
            if (value == kNumThreads) {
                counter.notify_all();

                // Wait some time for other threads to go to sleep.
                std::this_thread::sleep_for(1s);

                // One task per thread (assumes round-robin push to queues).
                for (std::size_t i{}; i < ts->NumberOfThreads(); ++i) {
                    ts->QueueTask([&store_id] { store_id(); });
                }
            }
            else {
                while (value != kNumThreads) {
                    counter.wait(value);
                    value = counter;
                }
            }
        };

        {
            TaskSystem ts{kNumThreads};

            // Wait some time for all threads to go to sleep.
            std::this_thread::sleep_for(1s);

            // One task per thread (assumes round-robin push to queues).
            for (std::size_t i{}; i < ts.NumberOfThreads(); ++i) {
                ts.QueueTask([&barrier, &ts] { barrier(&ts); });
            }
        }
        CHECK(tids.size() == kNumThreads);
    }
}
