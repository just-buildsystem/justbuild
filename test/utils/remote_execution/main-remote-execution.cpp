#define CATCH_CONFIG_RUNNER
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>

#include "catch2/catch.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/utils/logging/log_config.hpp"
#include "test/utils/test_env.hpp"

namespace {

void wait_for_grpc_to_shutdown() {
    // grpc_shutdown_blocking(); // not working
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

/// \brief Configure remote execution from test environment. In case the
/// environment variable is malformed, we write a message and stop execution.
/// \returns true   If remote execution was successfully configured.
[[nodiscard]] auto ConfigureRemoteExecution() -> bool {
    ReadCompatibilityFromEnv();
    HashFunction::SetHashType(Compatibility::IsCompatible()
                                  ? HashFunction::JustHash::Compatible
                                  : HashFunction::JustHash::Native);
    auto address = ReadRemoteAddressFromEnv();
    if (address and not RemoteExecutionConfig::SetRemoteAddress(*address)) {
        Logger::Log(LogLevel::Error, "parsing address '{}' failed.", *address);
        std::exit(EXIT_FAILURE);
    }
    for (auto const& property : ReadPlatformPropertiesFromEnv()) {
        if (not RemoteExecutionConfig::AddPlatformProperty(property)) {
            Logger::Log(
                LogLevel::Error, "parsing property '{}' failed.", property);
            std::exit(EXIT_FAILURE);
        }
    }
    return static_cast<bool>(RemoteExecutionConfig::RemoteAddress());
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    ConfigureLogging();

    // In case remote execution address is not valid, we skip tests. This is in
    // order to avoid tests being dependent on the environment.
    if (not ConfigureRemoteExecution()) {
        return EXIT_SUCCESS;
    }

    int result = Catch::Session().run(argc, argv);

    // valgrind fails if we terminate before grpc's async shutdown threads exit
    wait_for_grpc_to_shutdown();

    return result;
}
