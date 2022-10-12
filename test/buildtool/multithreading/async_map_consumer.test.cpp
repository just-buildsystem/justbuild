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

#include <cstdint>  // for fixed width integral types
#include <numeric>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/multithreading/async_map.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

auto FibonacciMapConsumer() -> AsyncMapConsumer<int, uint64_t> {
    auto value_creator = [](auto /*unused*/,
                            auto setter,
                            auto logger,
                            auto subcaller,
                            auto const& key) {
        if (key < 0) {
            (*logger)("index needs to be non-negative", true);
            return;
        }
        if (key < 2) {
            (*setter)(uint64_t{static_cast<uint64_t>(key)});
            return;
        }
        (*subcaller)(
            std::vector<int>{key - 2, key - 1},
            [setter](auto const& values) {
                (*setter)(*values[0] + *values[1]);
            },
            logger);
    };
    return AsyncMapConsumer<int, uint64_t>{value_creator};
}

auto FibOnEvenConsumer() -> AsyncMapConsumer<int, uint64_t> {
    auto value_creator = [](auto /*unused*/,
                            auto setter,
                            auto logger,
                            auto subcaller,
                            auto const& key) {
        if (key < 0) {
            (*logger)("index needs to be non-negative (and actually even)",
                      true);
            return;
        }
        if (key == 0) {
            (*setter)(uint64_t{static_cast<uint64_t>(0)});
            return;
        }
        if (key == 2) {
            (*setter)(uint64_t{static_cast<uint64_t>(1)});
            return;
        }
        (*subcaller)(
            std::vector<int>{key - 4, key - 2},
            [setter](auto const& values) {
                (*setter)(*values[0] + *values[1]);
            },
            logger);
    };
    return AsyncMapConsumer<int, uint64_t>{value_creator};
}

auto CountToMaxConsumer(int max_val, int step = 1, bool cycle = false)
    -> AsyncMapConsumer<int, uint64_t> {
    auto value_creator = [max_val, step, cycle](auto /*unused*/,
                                                auto setter,
                                                auto logger,
                                                auto subcaller,
                                                auto const& key) {
        if (key < 0 or key > max_val) {  // intentional bug: non-fatal abort
            (*logger)("index out of range", false);
            return;
        }
        if (key == max_val) {  // will never be reached if cycle==true
            (*setter)(uint64_t{static_cast<uint64_t>(key)});
            return;
        }
        auto next = key + step;
        if (cycle) {
            next %= max_val;
        }
        (*subcaller)(
            {next},
            [setter](auto const& values) { (*setter)(uint64_t{*values[0]}); },
            logger);
    };
    return AsyncMapConsumer<int, uint64_t>{value_creator};
}

TEST_CASE("Fibonacci", "[async_map_consumer]") {
    uint64_t result{};
    int const index{92};
    bool execution_failed = false;
    uint64_t const expected_result{7540113804746346429};
    auto mapconsumer = FibonacciMapConsumer();
    {
        TaskSystem ts;

        mapconsumer.ConsumeAfterKeysReady(
            &ts,
            {index},
            [&result](auto const& values) { result = *values[0]; },
            [&execution_failed](std::string const& /*unused*/,
                                bool /*unused*/) { execution_failed = true; });
    }
    CHECK(not execution_failed);
    CHECK(result == expected_result);
}

TEST_CASE("Values only used once nodes are marked ready",
          "[async_map_consumer]") {
    AsyncMapConsumer<int, bool> consume_when_ready{[](auto /*unused*/,
                                                      auto setter,
                                                      auto logger,
                                                      auto subcaller,
                                                      auto const& key) {
        if (key == 0) {
            (*setter)(true);
            return;
        }
        (*subcaller)(
            {key - 1},
            [setter, logger, key](auto const& values) {
                auto const ready_when_used = values[0];
                if (not ready_when_used) {
                    (*logger)(std::to_string(key), true);
                }
                (*setter)(true);
            },
            logger);
    }};
    std::vector<std::string> value_used_before_ready{};
    std::mutex vectorm;
    bool final_value{false};
    int const starting_index = 100;
    {
        TaskSystem ts;

        consume_when_ready.ConsumeAfterKeysReady(
            &ts,
            {starting_index},
            [&final_value](auto const& values) { final_value = values[0]; },
            [&value_used_before_ready, &vectorm](std::string const& key,
                                                 bool /*unused*/) {
                std::unique_lock l{vectorm};
                value_used_before_ready.push_back(key);
            });
    }
    CHECK(value_used_before_ready.empty());
    CHECK(final_value);
}

TEST_CASE("No subcalling necessary", "[async_map_consumer]") {
    AsyncMapConsumer<int, int> identity{
        [](auto /*unused*/,
           auto setter,
           [[maybe_unused]] auto logger,
           [[maybe_unused]] auto subcaller,
           auto const& key) { (*setter)(int{key}); }};
    std::vector<int> final_values{};
    std::vector<int> const keys{1, 23, 4};
    {
        TaskSystem ts;
        identity.ConsumeAfterKeysReady(
            &ts,
            keys,
            [&final_values](auto const& values) {
                std::transform(values.begin(),
                               values.end(),
                               std::back_inserter(final_values),
                               [](auto* val) { return *val; });
            },
            [](std::string const& /*unused*/, bool /*unused*/) {});
    }
    CHECK(keys == final_values);
}

TEST_CASE("FibOnEven", "[async_map_consumer]") {
    uint64_t result{};
    int const index{184};
    bool execution_failed = false;
    uint64_t const expected_result{7540113804746346429};
    auto mapconsumer = FibOnEvenConsumer();
    {
        TaskSystem ts;

        mapconsumer.ConsumeAfterKeysReady(
            &ts,
            {index},
            [&result](auto const& values) { result = *values[0]; },
            [&execution_failed](std::string const& /*unused*/,
                                bool /*unused*/) { execution_failed = true; });
    }
    CHECK(not execution_failed);
    CHECK(result == expected_result);
}

TEST_CASE("ErrorPropagation", "[async_map_consumer]") {
    int const index{183};  // Odd number, will fail
    bool execution_failed = false;
    bool consumer_called = false;
    std::atomic<int> fail_cont_counter{0};
    auto mapconsumer = FibOnEvenConsumer();
    {
        TaskSystem ts;

        mapconsumer.ConsumeAfterKeysReady(
            &ts,
            {index},
            [&consumer_called](auto const& /*unused*/) {
                consumer_called = true;
            },
            [&execution_failed](std::string const& /*unused*/,
                                bool /*unused*/) { execution_failed = true; },
            [&fail_cont_counter]() { fail_cont_counter++; });
    }
    CHECK(execution_failed);
    CHECK(!consumer_called);
    CHECK(fail_cont_counter == 1);
}

TEST_CASE("Failure detection", "[async_map_consumer]") {
    int const kMaxVal = 1000;  // NOLINT
    std::optional<int> value{std::nullopt};
    bool failed{};

    SECTION("Unfinished pending keys") {
        int const kStep{3};
        REQUIRE(std::lcm(kMaxVal, kStep) > kMaxVal);
        auto map = CountToMaxConsumer(kMaxVal, kStep);
        {
            TaskSystem ts;
            map.ConsumeAfterKeysReady(
                &ts,
                {0},
                [&value](auto const& values) { value = *values[0]; },
                [&failed](std::string const& /*unused*/, bool fatal) {
                    failed = failed or fatal;
                });
        }
        CHECK_FALSE(value);
        CHECK_FALSE(failed);
        CHECK_FALSE(map.DetectCycle());

        auto const pending = map.GetPendingKeys();
        CHECK_FALSE(pending.empty());

        std::vector<int> expected{};
        expected.reserve(kMaxVal + 1);
        for (int i = 0; i < kMaxVal + kStep; i += kStep) {
            expected.emplace_back(i);
        }
        CHECK_THAT(pending, Catch::Matchers::UnorderedEquals(expected));
    }

    SECTION("Cycle containing all unfinished keys") {
        auto map = CountToMaxConsumer(kMaxVal, 1, /*cycle=*/true);
        {
            TaskSystem ts;
            map.ConsumeAfterKeysReady(
                &ts,
                {0},
                [&value](auto const& values) { value = *values[0]; },
                [&failed](std::string const& /*unused*/, bool fatal) {
                    failed = failed or fatal;
                });
        }
        CHECK_FALSE(value);
        CHECK_FALSE(failed);

        auto const pending = map.GetPendingKeys();
        CHECK_FALSE(pending.empty());

        auto const cycle = map.DetectCycle();
        REQUIRE(cycle);

        // pending contains all keys from cycle (except last duplicate key)
        CHECK_THAT(pending,
                   Catch::Matchers::UnorderedEquals<int>(
                       {cycle->begin(), cycle->end() - 1}));

        // cycle contains keys in correct order
        std::vector<int> expected{};
        expected.reserve(kMaxVal + 1);
        for (int i = cycle->at(0); i < cycle->at(0) + kMaxVal + 1; ++i) {
            expected.emplace_back(i % kMaxVal);
        }
        CHECK_THAT(*cycle, Catch::Matchers::Equals(expected));
    }

    SECTION("No cycle and no unfinished keys") {
        auto map = CountToMaxConsumer(kMaxVal);
        {
            TaskSystem ts;
            map.ConsumeAfterKeysReady(
                &ts,
                {0},
                [&value](auto const& values) { value = *values[0]; },
                [&failed](std::string const& /*unused*/, bool fatal) {
                    failed = failed or fatal;
                });
        }
        REQUIRE(value);
        CHECK(*value == kMaxVal);
        CHECK_FALSE(failed);
        CHECK_FALSE(map.DetectCycle());
        CHECK(map.GetPendingKeys().empty());
    }
}
