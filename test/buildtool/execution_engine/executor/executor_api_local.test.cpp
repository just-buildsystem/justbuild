#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/execution_engine/executor/executor.hpp"
#include "test/buildtool/execution_engine/executor/executor_api.test.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Upload blob",
                 "[executor]") {
    TestBlobUpload([&] { return std::make_unique<LocalApi>(); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Compile hello world",
                 "[executor]") {
    TestHelloWorldCompilation([&] { return std::make_unique<LocalApi>(); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Compile greeter",
                 "[executor]") {
    TestGreeterCompilation([&] { return std::make_unique<LocalApi>(); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Upload and download trees",
                 "[executor]") {
    TestUploadAndDownloadTrees([&] { return std::make_unique<LocalApi>(); });
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Executor<LocalApi>: Retrieve output directories",
                 "[executor]") {
    TestRetrieveOutputDirectories([&] { return std::make_unique<LocalApi>(); });
}
