// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "src/buildtool/file_system/git_context.hpp"
#include "src/buildtool/file_system/git_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/other_tools/git_operations/git_config_settings.hpp"
#include "test/utils/logging/log_config.hpp"

#include <span>

extern "C" {
#include <git2.h>
}

namespace {

using anon_logger_t = std::function<void(const std::string&, bool)>;

auto const kGitConfigPath = std::filesystem::path{"gitconfig"};

}  // namespace

/// \brief Expects 2 mandatory arguments:
/// 1. the test type: "SSL" | "proxy"
/// 2. the remote URL to test
/// The third argument gives the expected result to check against:
///   - for type "SSL":    anything (check SSL) | missing arg (passthrough)
///   - for type "proxy":  proxy string (exact match) | missing arg (no proxy)
auto main(int argc, char* argv[]) -> int {
    try {
        ConfigureLogging();

        // start a git context, needed to read in the config file
        GitContext::Create();

        // handle args
        if (argc < 3) {
            Logger::Log(LogLevel::Error,
                        "Expected at least 3 args, but found {}",
                        argc);
            return 1;
        }
        auto args = std::span(argv, size_t(argc));
        std::string test_type{args[1]};  // type of test
        std::string test_url{args[2]};   // remote URL to test

        // setup dummy logger
        auto logger = std::make_shared<anon_logger_t>(
            []([[maybe_unused]] auto const& msg, [[maybe_unused]] bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Progress,
                            std::string(msg));
            });

        // read in the git config file
        git_config* cfg_ptr{nullptr};
        if (git_config_open_ondisk(&cfg_ptr, kGitConfigPath.c_str()) != 0) {
            Logger::Log(LogLevel::Error, "Open git config on disk failed");
            return 1;
        }
        auto cfg = std::shared_ptr<git_config>(cfg_ptr, config_closer);

        // run the method for given type
        if (test_type == "SSL") {
            auto callback =
                GitConfigSettings::GetSSLCallback(cfg, test_url, logger);
            if (not callback) {
                Logger::Log(LogLevel::Error, "Null SSL callback");
                return 1;
            }
            // check expected result
            auto expected_result = static_cast<int>(argc >= 4);
            auto actual_result = callback.value()(nullptr, 0, nullptr, nullptr);
            if (actual_result != expected_result) {
                Logger::Log(LogLevel::Error,
                            "Expected test result {}, but obtained {}",
                            expected_result,
                            actual_result);
                return 1;
            }
        }
        else if (test_type == "proxy") {
            auto proxy_info =
                GitConfigSettings::GetProxySettings(cfg, test_url, logger);
            if (not proxy_info) {
                Logger::Log(LogLevel::Error, "Missing proxy_info");
                return 1;
            }
            // check expected result
            auto expected_result =
                (argc >= 4) ? std::make_optional<std::string>(args[3])
                            : std::nullopt;
            auto actual_result = proxy_info.value();
            if (actual_result != expected_result) {
                Logger::Log(LogLevel::Error,
                            "Expected test result {}, but obtained {}",
                            expected_result ? *expected_result : "nullopt",
                            actual_result ? *actual_result : "nullopt");
                return 1;
            }
        }
        else {
            Logger::Log(LogLevel::Error,
                        R"(Expected test type {"SSL"|"proxy"}, but found {})",
                        test_type);
            return 1;
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Git config run test failed with:\n{}", ex.what());
        return 1;
    }
    return 0;
}
