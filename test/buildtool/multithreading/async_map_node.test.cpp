#include <mutex>
#include <string>
#include <thread>

#include "catch2/catch.hpp"
#include "src/buildtool/multithreading/async_map_node.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

TEST_CASE("No task is queued if the node is never ready", "[async_map_node]") {
    std::vector<int> tasks;
    std::mutex m;
    AsyncMapNode<int, bool> node_never_ready{0};
    {
        TaskSystem ts;
        CHECK_FALSE(
            node_never_ready.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
                std::unique_lock l{m};
                // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
                tasks.push_back(0);
            }));
        CHECK_FALSE(
            node_never_ready.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
                std::unique_lock l{m};
                // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
                tasks.push_back(1);
            }));
        CHECK_FALSE(
            node_never_ready.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
                std::unique_lock l{m};
                // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
                tasks.push_back(2);
            }));
    }
    CHECK(tasks.empty());
}

TEST_CASE("Value is set correctly", "[async_map_node]") {
    AsyncMapNode<int, bool> node{0};
    {
        TaskSystem ts;
        node.SetAndQueueAwaitingTasks(&ts, true);
    }
    CHECK(node.GetValue());
}

TEST_CASE("Tasks are queued correctly", "[async_map_node]") {
    AsyncMapNode<int, std::string> node{0};
    std::vector<int> tasks;
    std::mutex m;
    {
        TaskSystem ts;
        CHECK_FALSE(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(0);
        }));
        CHECK_FALSE(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(1);
        }));
        CHECK_FALSE(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(2);
        }));

        {
            std::unique_lock l{m};
            CHECK(tasks.empty());
        }
        node.SetAndQueueAwaitingTasks(&ts, "ready");
        CHECK(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(3);
        }));
        CHECK(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(4);
        }));
        CHECK(node.AddOrQueueAwaitingTask(&ts, [&tasks, &m]() {
            std::unique_lock l{m};
            // NOLINTNEXTLINE(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
            tasks.push_back(5);
        }));
    }
    CHECK(node.GetValue() == "ready");
    CHECK_THAT(
        tasks,
        Catch::Matchers::UnorderedEquals(std::vector<int>{0, 1, 2, 3, 4, 5}));
}
