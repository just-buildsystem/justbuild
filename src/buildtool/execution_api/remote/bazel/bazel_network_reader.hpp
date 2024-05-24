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

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/file_system/git_repo.hpp"

class BazelNetworkReader final {
  public:
    using DumpCallback = std::function<bool(std::string const&)>;

    explicit BazelNetworkReader(
        BazelNetwork const& network,
        std::optional<ArtifactDigest> request_remote_tree =
            std::nullopt) noexcept;

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

  private:
    using DirectoryMap =
        std::unordered_map<bazel_re::Digest, bazel_re::Directory>;

    BazelNetwork const& network_;
    std::optional<DirectoryMap> auxiliary_map_;

    [[nodiscard]] static auto MakeAuxiliaryMap(
        std::vector<bazel_re::Directory>&& full_tree) noexcept
        -> std::optional<DirectoryMap>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP
