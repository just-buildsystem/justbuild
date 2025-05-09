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

#include "src/buildtool/execution_api/local/local_api.hpp"

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/buildtool/execution_api/common/api_test.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {
class FactoryApi final {
  public:
    explicit FactoryApi(
        gsl::not_null<LocalContext const*> const& local_context) noexcept
        : local_context_{*local_context} {}

    [[nodiscard]] auto operator()() const -> IExecutionApi::Ptr {
        return IExecutionApi::Ptr{new LocalApi{&local_context_}};
    }

  private:
    LocalContext const& local_context_;
};

}  // namespace

TEST_CASE("LocalAPI: No input, no output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestNoInputNoOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: No input, create output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestNoInputCreateOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: One input copied to output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestOneInputCopiedToOutput(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Non-zero exit code, create output", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestNonZeroExitCodeCreateOutput(api_factory, {});
}

TEST_CASE("LocalAPI: Retrieve two identical trees to path", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestRetrieveTwoIdenticalTreesToPath(
        api_factory, {}, "two_trees", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Retrieve file and symlink with same content to path",
          "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestRetrieveFileAndSymlinkWithSameContentToPath(
        api_factory, {}, "file_and_symlink", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Retrieve mixed blobs and trees", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestRetrieveMixedBlobsAndTrees(
        api_factory, {}, "blobs_and_trees", /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Create directory prior to execution", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestCreateDirPriorToExecution(api_factory, {}, /*is_hermetic=*/true);
}

TEST_CASE("LocalAPI: Collect file and directory symlinks", "[execution_api]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    auto const local_exec_config = CreateLocalExecConfig();
    // pack the local context instances to be passed to LocalApi
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};
    FactoryApi api_factory(&local_context);

    TestSymlinkCollection(api_factory, {});
}
