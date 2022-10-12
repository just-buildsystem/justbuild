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
