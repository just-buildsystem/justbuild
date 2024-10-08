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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_FILE_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_FILE_HPP

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#ifdef __unix__
#include <sys/time.h>
#else
#error "Non-unix is not supported yet"
#endif

#include "fmt/chrono.h"
#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Thread-safe map of mutexes.
template <class TKey>
class MutexMap {
  public:
    /// \brief Create mutex for key and run callback if successfully created.
    /// Callback is executed while the internal map is still held exclusively.
    void Create(TKey const& key, std::function<void()> const& callback) {
        std::lock_guard lock(mutex_);
        if (not map_.contains(key)) {
            [[maybe_unused]] auto& mutex = map_[key];
            callback();
        }
    }
    /// \brief Get mutex for key, creates mutex if key does not exist.
    [[nodiscard]] auto Get(TKey const& key) noexcept -> std::mutex& {
        std::lock_guard lock(mutex_);
        return map_[key];
    }

  private:
    std::mutex mutex_;
    std::unordered_map<TKey, std::mutex> map_;
};

class LogSinkFile final : public ILogSink {
  public:
    enum class Mode : std::uint8_t {
        Append,    ///< Append if log file already exists.
        Overwrite  ///< Overwrite log file with each new program instantiation.
    };

    static auto CreateFactory(std::filesystem::path const& file_path,
                              Mode file_mode = Mode::Append) -> LogSinkFactory {
        return
            [=] { return std::make_shared<LogSinkFile>(file_path, file_mode); };
    }

    LogSinkFile(std::filesystem::path const& file_path, Mode file_mode)
        : file_path_{std::filesystem::weakly_canonical(file_path).string()} {
        // create file mutex for canonical path
        FileMutexes().Create(file_path_, [&] {
            if (file_mode == Mode::Overwrite) {
                // clear file contents
                if (gsl::owner<FILE*> file =
                        std::fopen(file_path_.c_str(), "w")) {
                    std::fclose(file);
                }
            }
        });
    }
    ~LogSinkFile() noexcept final = default;
    LogSinkFile(LogSinkFile const&) noexcept = delete;
    LogSinkFile(LogSinkFile&&) noexcept = delete;
    auto operator=(LogSinkFile const&) noexcept -> LogSinkFile& = delete;
    auto operator=(LogSinkFile&&) noexcept -> LogSinkFile& = delete;

    /// \brief Thread-safe emitting of log messages to file.
    /// Race-conditions for file writes are resolved via a separate mutexes for
    /// every canonical file path shared across all instances of this class.
    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg) const noexcept final {
#ifdef __unix__  // support nanoseconds for timestamp
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        auto timestamp = fmt::format(
            "{:%Y-%m-%d %H:%M:%S}.{}", fmt::localtime(ts.tv_sec), ts.tv_nsec);
#else
        auto timestamp = fmt::format(
            "{:%Y-%m-%d %H:%M:%S}", fmt::localtime(std::time(nullptr));
#endif

        std::ostringstream id{};
        id << "thread:" << std::this_thread::get_id();
        auto thread = id.str();

        auto prefix = fmt::format(
            "{}, [{}] {}", thread, timestamp, LogLevelToString(level));

        if (logger != nullptr) {
            // append logger name
            prefix = fmt::format("{} ({})", prefix, logger->Name());
        }
        prefix = fmt::format("{}:", prefix);
        const auto* cont_prefix = "  ";

        {
            std::lock_guard lock{FileMutexes().Get(file_path_)};
            if (gsl::owner<FILE*> file = std::fopen(file_path_.c_str(), "a")) {
                using it = std::istream_iterator<ILogSink::Line>;
                std::istringstream iss{msg};
                for_each(it{iss}, it{}, [&](auto const& line) {
                    fmt::print(file, "{} {}\n", prefix, line);
                    prefix = cont_prefix;
                });
                std::fclose(file);
            }
        }
    }

  private:
    std::string file_path_;

    [[nodiscard]] static auto FileMutexes() noexcept -> MutexMap<std::string>& {
        static MutexMap<std::string> instance{};
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_FILE_HPP
