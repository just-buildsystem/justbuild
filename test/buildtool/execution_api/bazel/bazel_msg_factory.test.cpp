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

#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

namespace {
/// \brief Create a blob from the content found in file or symlink pointed to by
/// given path.
[[nodiscard]] inline auto CreateBlobFromPath(
    std::filesystem::path const& fpath,
    HashFunction hash_function) noexcept -> std::optional<ArtifactBlob> {
    auto const type = FileSystemManager::Type(fpath, /*allow_upwards=*/true);
    if (not type) {
        return std::nullopt;
    }
    auto const content = FileSystemManager::ReadContentAtPath(fpath, *type);
    if (not content.has_value()) {
        return std::nullopt;
    }
    return ArtifactBlob{ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                            hash_function, *content),
                        *content,
                        IsExecutableObject(*type)};
}
}  // namespace

TEST_CASE("Bazel internals: MessageFactory", "[execution_api]") {
    std::filesystem::path workspace{"test/buildtool/storage/data"};

    std::filesystem::path subdir1 = workspace / "subdir1";
    std::filesystem::path subdir2 = subdir1 / "subdir2";
    std::filesystem::path file1 = subdir1 / "file1";
    std::filesystem::path file2 = subdir2 / "file2";

    // create a symlink
    std::filesystem::path link = subdir1 / "link";
    REQUIRE(FileSystemManager::CreateSymlink("file1", link));

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    // create the corresponding blobs
    auto file1_blob = CreateBlobFromPath(file1, hash_function);
    auto file2_blob = CreateBlobFromPath(file2, hash_function);
    auto link_blob = CreateBlobFromPath(link, hash_function);

    CHECK(file1_blob);
    CHECK(file2_blob);
    CHECK(link_blob);

    // both files are the same and should result in identical blobs
    CHECK(*file1_blob->data == *file2_blob->data);
    CHECK(file1_blob->digest.hash() == file2_blob->digest.hash());
    CHECK(file1_blob->digest.size() == file2_blob->digest.size());

    // create known artifacts
    auto artifact1_opt =
        ArtifactDescription::CreateKnown(file1_blob->digest, ObjectType::File)
            .ToArtifact();
    auto artifact1 = DependencyGraph::ArtifactNode{std::move(artifact1_opt)};

    auto artifact2_opt =
        ArtifactDescription::CreateKnown(file2_blob->digest, ObjectType::File)
            .ToArtifact();
    auto artifact2 = DependencyGraph::ArtifactNode{std::move(artifact2_opt)};

    auto artifact3_opt =
        ArtifactDescription::CreateKnown(link_blob->digest, ObjectType::Symlink)
            .ToArtifact();
    auto artifact3 = DependencyGraph::ArtifactNode{std::move(artifact3_opt)};

    // create directory tree
    auto tree =
        DirectoryTree::FromNamedArtifacts({{file1.string(), &artifact1},
                                           {file2.string(), &artifact2},
                                           {link.string(), &artifact3}});
    CHECK(tree.has_value());

    // a mapping between digests and content is needed; usually via a concrete
    // API one gets this content either locally or from the network
    std::unordered_map<ArtifactDigest, std::filesystem::path> fake_cas{
        {file1_blob->digest, file1},
        {file2_blob->digest, file2},
        {link_blob->digest, link}};

    // create blobs via tree
    std::unordered_set<ArtifactBlob> blobs{};
    REQUIRE(BazelMsgFactory::CreateDirectoryDigestFromTree(
        *tree,
        [&fake_cas](std::vector<ArtifactDigest> const& digests,
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
        [&blobs](ArtifactBlob&& blob) {
            blobs.emplace(std::move(blob));
            return true;
        }));

    // TODO(aehlig): also check total number of DirectoryNode blobs in container
}
