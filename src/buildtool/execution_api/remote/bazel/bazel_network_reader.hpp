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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/file_system/git_repo.hpp"

class BazelNetworkReader final {
  public:
    using DumpCallback = std::function<bool(std::string const&)>;

    BazelNetworkReader(std::string instance_name,
                       BazelCasClient const& cas) noexcept;

    BazelNetworkReader(
        BazelNetworkReader&& other,
        std::optional<ArtifactDigest> request_remote_tree) noexcept;

    [[nodiscard]] auto ReadDirectory(ArtifactDigest const& digest)
        const noexcept -> std::optional<bazel_re::Directory>;

    [[nodiscard]] auto ReadGitTree(ArtifactDigest const& digest) const noexcept
        -> std::optional<GitRepo::tree_entries_t>;

    [[nodiscard]] auto DumpRawTree(Artifact::ObjectInfo const& info,
                                   DumpCallback const& dumper) const noexcept
        -> bool;

    [[nodiscard]] auto DumpBlob(Artifact::ObjectInfo const& info,
                                DumpCallback const& dumper) const noexcept
        -> bool;

    [[nodiscard]] auto ReadSingleBlob(ArtifactDigest const& digest)
        const noexcept -> std::optional<ArtifactBlob>;

  private:
    using DirectoryMap =
        std::unordered_map<ArtifactDigest, bazel_re::Directory>;

    std::string const instance_name_;
    BazelCasClient const& cas_;
    std::optional<DirectoryMap> auxiliary_map_;

    [[nodiscard]] static auto MakeAuxiliaryMap(
        std::vector<bazel_re::Directory>&& full_tree) noexcept
        -> std::optional<DirectoryMap>;

    [[nodiscard]] auto BatchReadBlobs(
        std::vector<bazel_re::Digest> const& blobs) const noexcept
        -> std::vector<ArtifactBlob>;

    [[nodiscard]] static auto Validate(BazelBlob const& blob) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP
