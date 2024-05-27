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
#include <unordered_map>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/blob_creator.hpp"

TEST_CASE("Bazel internals: MessageFactory", "[execution_api]") {
    std::filesystem::path workspace{"test/buildtool/storage/data"};

    std::filesystem::path subdir1 = workspace / "subdir1";
    std::filesystem::path subdir2 = subdir1 / "subdir2";
    std::filesystem::path file1 = subdir1 / "file1";
    std::filesystem::path file2 = subdir2 / "file2";

    // create a symlink
    std::filesystem::path link = subdir1 / "link";
    REQUIRE(FileSystemManager::CreateSymlink("file1", link));

    // create the corresponding blobs
    auto file1_blob = CreateBlobFromPath(file1);
    auto file2_blob = CreateBlobFromPath(file2);
    auto link_blob = CreateBlobFromPath(link);

    CHECK(file1_blob);
    CHECK(file2_blob);
    CHECK(link_blob);

    // both files are the same and should result in identical blobs
    CHECK(*file1_blob->data == *file2_blob->data);
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

    auto artifact3_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            NativeSupport::Unprefix(link_blob->digest.hash()),
            static_cast<std::size_t>(link_blob->digest.size_bytes()),
            ObjectType::Symlink));
    CHECK(artifact3_opt.has_value());
    auto artifact3 = DependencyGraph::ArtifactNode{std::move(*artifact3_opt)};

    // create directory tree
    auto tree =
        DirectoryTree::FromNamedArtifacts({{file1.string(), &artifact1},
                                           {file2.string(), &artifact2},
                                           {link.string(), &artifact3}});
    CHECK(tree.has_value());

    // a mapping between digests and content is needed; usually via a concrete
    // API one gets this content either locally or from the network
    std::unordered_map<bazel_re::Digest, std::filesystem::path> fake_cas{
        {file1_blob->digest, file1},
        {file2_blob->digest, file2},
        {link_blob->digest, link}};

    // create blobs via tree
    BazelBlobContainer blobs{};
    REQUIRE(BazelMsgFactory::CreateDirectoryDigestFromTree(
        *tree,
        [&fake_cas](std::vector<bazel_re::Digest> const& digests,
                    std::vector<std::string>* targets) {
            targets->reserve(digests.size());
            for (auto const& digest : digests) {
                REQUIRE(fake_cas.contains(digest));
                auto fpath = fake_cas[digest];
                if (FileSystemManager::IsNonUpwardsSymlink(
                        fpath, /*non_strict=*/true)) {
                    auto content = FileSystemManager::ReadSymlink(fpath);
                    REQUIRE(content);
                    targets->emplace_back(*content);
                }
                else {
                    auto content = FileSystemManager::ReadFile(fpath);
                    REQUIRE(content);
                    targets->emplace_back(*content);
                }
            }
        },
        [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); }));

    // TODO(aehlig): also check total number of DirectoryNode blobs in container
}
