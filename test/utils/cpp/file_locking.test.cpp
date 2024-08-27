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

#include "src/utils/cpp/file_locking.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/utils/cpp/atomic.hpp"

namespace {
[[nodiscard]] auto GetTestDir() noexcept -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() / "test/other_tools";
}

[[nodiscard]] auto GetLockDirPath(int id) noexcept -> std::filesystem::path {
    auto lock_file = std::to_string(id) + std::string{".lock"};
    return GetTestDir() / lock_file;
}
}  // namespace

TEST_CASE("Multi-file locking", "[file_locking]") {
    // Test locking and unlocking. Each thread will have one lock.
    // setup threading
    constexpr auto kNumThreads = 50;  // increasing it too much will fail
    constexpr auto kNumLocks = 5;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    for (int id{}; id < kNumThreads; ++id) {
        threads.emplace_back(
            [&starting_signal](int tid) {
                starting_signal.wait(false);
                // cases based on id
                auto flockpath = GetLockDirPath(tid % kNumLocks);
                // Get lock
                auto lock = LockFile::Acquire(flockpath, /*is_shared=*/false);
                REQUIRE(lock);
                // Do some "work"
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                // lock released automatically when out of scope
            },
            id);
    }

    starting_signal = true;
    starting_signal.notify_all();

    // wait for threads to finish
    for (auto& thread : threads) {
        thread.join();
    }
}
