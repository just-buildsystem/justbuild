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

#define CATCH_CONFIG_RUNNER
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/utils/logging/log_config.hpp"
#include "test/utils/serve_service/test_serve_config.hpp"
#include "test/utils/shell_quoting.hpp"
#include "test/utils/test_env.hpp"

namespace {

auto const kBundlePath =
    std::string{"test/buildtool/file_system/data/test_repo.bundle"};
auto const kBundlePathSymlinks =
    std::string{"test/buildtool/file_system/data/test_repo_symlinks.bundle"};

void wait_for_grpc_to_shutdown() {
    // grpc_shutdown_blocking(); // not working
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

[[nodiscard]] auto CloneRepo(std::filesystem::path const& repo_path,
                             std::string const& bundle,
                             bool is_bare = false) noexcept -> bool {
    auto cmd = fmt::format("git clone {}{} {}",
                           is_bare ? "--bare " : "",
                           QuoteForShell(bundle),
                           QuoteForShell(repo_path.string()));
    return (std::system(cmd.c_str()) == 0);
}

[[nodiscard]] auto CreateServeTestRepo(std::filesystem::path const& repo_path,
                                       std::string const& bundle,
                                       bool is_bare = false) noexcept -> bool {
    if (not CloneRepo(repo_path, bundle, is_bare)) {
        return false;
    }
    auto cmd =
        fmt::format("git --git-dir={} --work-tree={} checkout master",
                    QuoteForShell(is_bare ? repo_path.string()
                                          : (repo_path / ".git").string()),
                    QuoteForShell(repo_path.string()));
    return (std::system(cmd.c_str()) == 0);
}

/// \brief Configure serve service from test environment. In case the
/// environment variable is malformed, we write a message and stop execution.
/// The availability of the serve service known repositories (from known test
/// bundles) is also ensured.
/// \returns true   If serve service was successfully configured.
[[nodiscard]] auto ConfigureServeService() -> bool {
    // just serve shares here compatibility and authentication args with
    // remote execution, so no need to do those again

    // Ensure the config can be read from the environment
    auto config = TestServeConfig::ReadServeConfigFromEnvironment();
    if (not config or not config->remote_address) {
        return false;
    }

    // now actually populate the serve repositories, one bare and one non-bare
    if (config->known_repositories.size() != 2) {
        Logger::Log(LogLevel::Error,
                    "Expected 2 serve repositories in test env.");
        std::exit(EXIT_FAILURE);
    }

    auto const& bare_repo = config->known_repositories[0];
    auto const& nonbare_repo = config->known_repositories[1];
    if (not CreateServeTestRepo(bare_repo, kBundlePath, /*is_bare=*/true) or
        not CreateServeTestRepo(
            nonbare_repo, kBundlePathSymlinks, /*is_bare=*/false)) {
        Logger::Log(LogLevel::Error,
                    "Failed to setup serve service repositories.");
        std::exit(EXIT_FAILURE);
    }

    return true;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    ConfigureLogging();
    ReadCompatibilityFromEnv();

    // Setup of serve service, including known repositories.
    if (not ConfigureServeService()) {
        return EXIT_FAILURE;
    }

    /**
     * The current implementation of libgit2 uses pthread_key_t incorrectly
     * on POSIX systems to handle thread-specific data, which requires us to
     * explicitly make sure the main thread is the first one to call
     * git_libgit2_init. Future versions of libgit2 will hopefully fix this.
     */
    GitContext::Create();

    int result = Catch::Session().run(argc, argv);

    // valgrind fails if we terminate before grpc's async shutdown threads exit
    wait_for_grpc_to_shutdown();

    return result;
}
