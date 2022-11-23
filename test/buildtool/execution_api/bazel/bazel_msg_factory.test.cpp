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

#include <filesystem>

#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/object_type.hpp"

TEST_CASE("Bazel internals: MessageFactory", "[execution_api]") {
    std::filesystem::path workspace{"test/buildtool/execution_api/data"};

    std::filesystem::path subdir1 = workspace / "subdir1";
    std::filesystem::path subdir2 = subdir1 / "subdir2";
    std::filesystem::path file1 = subdir1 / "file1";
    std::filesystem::path file2 = subdir2 / "file2";

    auto file1_blob = CreateBlobFromFile(file1);
    auto file2_blob = CreateBlobFromFile(file2);

    CHECK(file1_blob);
    CHECK(file2_blob);

    // both files are the same and should result in identical blobs
    CHECK(file1_blob->data == file2_blob->data);
    CHECK(file1_blob->digest.hash() == file2_blob->digest.hash());
    CHECK(file1_blob->digest.size_bytes() == file2_blob->digest.size_bytes());

    // create known artifacts
    auto artifact1_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            NativeSupport::Unprefix(file1_blob->digest.hash()),
            static_cast<std::size_t>(file1_blob->digest.size_bytes()),
            ObjectType::File));
    CHECK(artifact1_opt.has_value());
    auto artifact1 = DependencyGraph::ArtifactNode{std::move(*artifact1_opt)};

    auto artifact2_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            NativeSupport::Unprefix(file2_blob->digest.hash()),
            static_cast<std::size_t>(file2_blob->digest.size_bytes()),
            ObjectType::File));
    CHECK(artifact2_opt.has_value());
    auto artifact2 = DependencyGraph::ArtifactNode{std::move(*artifact2_opt)};

    // create directory tree
    auto tree = DirectoryTree::FromNamedArtifacts(
        {{file1.string(), &artifact1}, {file2.string(), &artifact2}});
    CHECK(tree.has_value());

    // create blobs via tree
    BlobContainer blobs{};
    REQUIRE(BazelMsgFactory::CreateDirectoryDigestFromTree(
        *tree, [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); }));

    // TODO(aehlig): also check total number of DirectoryNode blobs in container
}
