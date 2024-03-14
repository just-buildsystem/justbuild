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

#include <fstream>
#include <iostream>
#include <thread>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"
#include "catch2/matchers/catch_matchers_all.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"

[[nodiscard]] static auto NumberOfLines(std::filesystem::path const& file_path)
    -> int {
    std::ifstream file(file_path);
    std::string line{};
    int number_of_lines{};
    while (std::getline(file, line)) {
        ++number_of_lines;
    }
    return number_of_lines;
}

[[nodiscard]] static auto GetLines(std::filesystem::path const& file_path)
    -> std::vector<std::string> {
    std::ifstream file(file_path);
    std::string line{};
    std::vector<std::string> lines{};
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

TEST_CASE("LogSinkFile", "[logging]") {
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(false /*no color*/)});

    // cleanup
    std::string filename{"test/test.log"};
    CHECK(FileSystemManager::RemoveFile(filename));
    REQUIRE(not FileSystemManager::IsFile(filename));

    // create test log file
    REQUIRE(FileSystemManager::WriteFile("somecontent\n", filename));
    REQUIRE(FileSystemManager::IsFile(filename));
    CHECK(NumberOfLines(filename) == 1);

    SECTION("Overwrite mode") {
        LogSinkFile sink{filename, LogSinkFile::Mode::Overwrite};

        sink.Emit(nullptr, LogLevel::Info, "first");
        sink.Emit(nullptr, LogLevel::Info, "second");
        sink.Emit(nullptr, LogLevel::Info, "third");

        // read file and check line numbers
        CHECK(NumberOfLines(filename) == 3);
    }

    SECTION("Append mode") {
        LogSinkFile sink{filename, LogSinkFile::Mode::Append};

        sink.Emit(nullptr, LogLevel::Info, "first");
        sink.Emit(nullptr, LogLevel::Info, "second");
        sink.Emit(nullptr, LogLevel::Info, "third");

        // read file and check line numbers
        CHECK(NumberOfLines(filename) == 4);
    }

    SECTION("Thread-safety") {
        int const num_threads = 20;
        LogSinkFile sink{filename, LogSinkFile::Mode::Append};

        // start threads, each emitting a log message
        std::vector<std::thread> threads{};
        for (int id{}; id < num_threads; ++id) {
            threads.emplace_back(
                [&](int tid) {
                    sink.Emit(nullptr,
                              LogLevel::Info,
                              "this is thread " + std::to_string(tid));
                },
                id);
        }

        // wait for threads to finish
        for (auto& thread : threads) {
            thread.join();
        }

        // read file and check line numbers
        auto lines = GetLines(filename);
        CHECK(lines.size() == num_threads + 1);

        // check for corrupted content
        for (auto const& line : lines) {
            CHECK_THAT(
                line,
                Catch::Matchers::ContainsSubstring("somecontent") ||
                    Catch::Matchers::ContainsSubstring("this is thread"));
        }
    }
}
