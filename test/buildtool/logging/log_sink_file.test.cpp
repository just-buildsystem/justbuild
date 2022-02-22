#include <fstream>
#include <thread>

#include "catch2/catch.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_config.hpp"
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
        using Catch::Matchers::Contains;
        for (auto const& line : lines) {
            CHECK_THAT(line,
                       Contains("somecontent") or Contains("this is thread "));
        }
    }
}
