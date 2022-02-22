#define CATCH_CONFIG_RUNNER
#include "catch2/catch.hpp"
#include "test/utils/logging/log_config.hpp"

auto main(int argc, char* argv[]) -> int {
    ConfigureLogging();
    return Catch::Session().run(argc, argv);
}
