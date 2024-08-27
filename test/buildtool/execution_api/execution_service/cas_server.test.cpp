// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/execution_service/cas_server.hpp"

#include <string>

#include "catch2/catch_test_macros.hpp"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {
[[nodiscard]] auto Upload(
    gsl::not_null<bazel_re::ContentAddressableStorage::Service*> const&
        cas_server,
    std::string const& instance_name,
    bazel_re::Digest const& digest,
    std::string const& content) noexcept -> grpc::Status {
    auto request = bazel_re::BatchUpdateBlobsRequest{};
    request.set_instance_name(instance_name);
    auto* req = request.add_requests();
    req->mutable_digest()->CopyFrom(digest);
    req->set_data(content);
    auto response = bazel_re::BatchUpdateBlobsResponse{};
    return cas_server->BatchUpdateBlobs(nullptr, &request, &response);
}
}  // namespace

TEST_CASE("CAS Service: upload incomplete tree", "[execution_service]") {
    // For compatible mode tree invariants aren't checked.
    if (Compatibility::IsCompatible()) {
        return;
    }

    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig const local_exec_config{};

    // pack the local context instances to be passed
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    auto cas_server = CASServiceImpl{&local_context};
    auto instance_name = std::string{"remote-execution"};

    // Create an empty tree.
    auto empty_entries = GitRepo::tree_entries_t{};
    auto empty_tree = GitRepo::CreateShallowTree(empty_entries);
    REQUIRE(empty_tree);
    auto empty_tree_digest = ArtifactDigest::Create<ObjectType::Tree>(
        storage_config.Get().hash_function, empty_tree->second);

    // Create a tree containing the empty tree.
    auto entries = GitRepo::tree_entries_t{};
    entries[empty_tree->first].emplace_back("empty_tree", ObjectType::Tree);
    auto tree = GitRepo::CreateShallowTree(entries);
    REQUIRE(tree);
    auto tree_digest = ArtifactDigest::Create<ObjectType::Tree>(
        storage_config.Get().hash_function, tree->second);

    // Upload tree. The tree invariant is violated, thus, a negative answer is
    // expected.
    auto status = Upload(&cas_server, instance_name, tree_digest, tree->second);
    CHECK(not status.ok());

    // Upload empty tree.
    status = Upload(
        &cas_server, instance_name, empty_tree_digest, empty_tree->second);
    CHECK(status.ok());

    // Upload tree again. This time, the tree invariant is honored and a
    // positive answer is expected.
    status = Upload(&cas_server, instance_name, tree_digest, tree->second);
    CHECK(status.ok());
}
