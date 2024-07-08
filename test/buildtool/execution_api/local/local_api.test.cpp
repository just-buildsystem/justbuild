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
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {
class FactoryApi final {
  public:
    explicit FactoryApi(
        gsl::not_null<StorageConfig const*> const& storage_config,
        gsl::not_null<Storage const*> const& storage,
        gsl::not_null<LocalExecutionConfig const*> const&
            local_exec_config) noexcept
        : storage_config_{*storage_config},
          storage_{*storage},
          local_exec_config_{*local_exec_config} {}

    [[nodiscard]] auto operator()() const -> IExecutionApi::Ptr {
        return IExecutionApi::Ptr{
            new LocalApi{&storage_config_, &storage_, &local_exec_config_}};
    }

  private:
    StorageConfig const& storage_config_;
    Storage const& storage_;
    LocalExecutionConfig const& local_exec_config_;
};

}  // namespace

TEST_CASE("LocalAPI: No input, no output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestNoInputNoOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: No input, create output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestNoInputCreateOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: One input copied to output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestOneInputCopiedToOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Non-zero exit code, create output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestNonZeroExitCodeCreateOutput(api_factory, {});
}

TEST_CASE("LocalAPI: Retrieve two identical trees to path", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestRetrieveTwoIdenticalTreesToPath(
        api_factory, {}, "two_trees", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Retrieve file and symlink with same content to path",
          "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestRetrieveFileAndSymlinkWithSameContentToPath(
        api_factory, {}, "file_and_symlink", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Retrieve mixed blobs and trees", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestRetrieveMixedBlobsAndTrees(
        api_factory, {}, "blobs_and_trees", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Create directory prior to execution", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = SetLauncher();
    FactoryApi api_factory(&storage_config.Get(), &storage, &local_exec_config);

    TestCreateDirPriorToExecution(api_factory, {}, /*is_hermetic=*/true);
}
