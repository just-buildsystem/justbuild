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

#define CATCH_CONFIG_RUNNER
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

#include "catch2/catch_session.hpp"
#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_context.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "test/utils/logging/log_config.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"
#include "test/utils/test_env.hpp"

namespace {

void wait_for_grpc_to_shutdown() {
    // grpc_shutdown_blocking(); // not working
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

/// \brief Configure remote execution from test environment. In case the
/// environment variable is malformed, we write a message and stop execution.
/// \returns true   If remote execution was successfully configured.
void ConfigureRemoteExecution() {
    ReadCompatibilityFromEnv();

    // Ensure authentication config is available
    if (not TestAuthConfig::ReadFromEnvironment()) {
        std::exit(EXIT_FAILURE);
    }

    HashFunction::Instance().SetHashType(
        Compatibility::IsCompatible() ? HashFunction::JustHash::Compatible
                                      : HashFunction::JustHash::Native);

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    if (not remote_config or remote_config->remote_address == std::nullopt) {
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    ConfigureLogging();

    ConfigureRemoteExecution();

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
