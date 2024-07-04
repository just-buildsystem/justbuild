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

#include <cstdlib>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/local.hpp"

namespace {
class FactoryApi final {
  public:
    explicit FactoryApi(
        gsl::not_null<Storage const*> const& storage,
        gsl::not_null<StorageConfig const*> const& storage_config) noexcept
        : storage_{*storage}, storage_config_{*storage_config} {}

    [[nodiscard]] auto operator()() const -> IExecutionApi::Ptr {
        return IExecutionApi::Ptr{
            new LocalApi{&StorageConfig::Instance(), &storage_}};
    }

  private:
    Storage const& storage_;
    StorageConfig const& storage_config_;
};

}  // namespace

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, no output",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestNoInputNoOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: No input, create output",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestNoInputCreateOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: One input copied to output",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestOneInputCopiedToOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Non-zero exit code, create output",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestNonZeroExitCodeCreateOutput(api_factory, {});
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve two identical trees to path",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestRetrieveTwoIdenticalTreesToPath(
        api_factory, {}, "two_trees", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve file and symlink with same content to "
                 "path",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestRetrieveFileAndSymlinkWithSameContentToPath(
        api_factory, {}, "file_and_symlink", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Retrieve mixed blobs and trees",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestRetrieveMixedBlobsAndTrees(
        api_factory, {}, "blobs_and_trees", /*is_hermetic=*/true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAPI: Create directory prior to execution",
                 "[execution_api]") {
    auto const storage = Storage::Create(&StorageConfig::Instance());
    FactoryApi api_factory(&storage, &StorageConfig::Instance());
    TestCreateDirPriorToExecution(api_factory, {}, /*is_hermetic=*/true);
}
