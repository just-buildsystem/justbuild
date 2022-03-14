#include <cstdlib>
#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/local.hpp"

namespace {

auto api_factory = []() { return IExecutionApi::Ptr{new LocalApi()}; };

}  // namespace

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, no output",
                 "[execution_api]") {
    TestNoInputNoOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, create output",
                 "[execution_api]") {
    TestNoInputCreateOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: One input copied to output",
                 "[execution_api]") {
    TestOneInputCopiedToOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Non-zero exit code, create output",
                 "[execution_api]") {
    TestNonZeroExitCodeCreateOutput(api_factory, {});
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve two identical trees to path",
                 "[execution_api]") {
    TestRetrieveTwoIdenticalTreesToPath(
        api_factory, {}, "local", /*is_hermetic=*/true);
}
