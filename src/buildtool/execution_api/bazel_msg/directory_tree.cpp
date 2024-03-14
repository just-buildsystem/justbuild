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

#include "directory_tree.hpp"

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"

auto DirectoryTree::AddArtifact(std::filesystem::path const& path,
                                Artifact const* artifact) noexcept -> bool {
    auto const norm_path = path.lexically_normal();
    if (norm_path.empty() or not FileSystemManager::IsRelativePath(norm_path)) {
        return false;
    }
    auto it = norm_path.begin();
    try {
        return AddArtifact(&it, norm_path.end(), artifact);
    } catch (...) {
        return false;
    }
}

auto DirectoryTree::FromNamedArtifacts(
    std::vector<DependencyGraph::NamedArtifactNodePtr> const&
        artifacts) noexcept -> std::optional<DirectoryTreePtr> {
    auto dir_tree = std::make_unique<DirectoryTree>();
    for (auto const& [local_path, node] : artifacts) {
        auto const* artifact = &node->Content();
        if (not dir_tree->AddArtifact(local_path, artifact)) {
            Logger::Log(LogLevel::Error,
                        "failed to add artifact {} ({}) to directory tree",
                        local_path,
                        artifact->Digest().value_or(ArtifactDigest{}).hash());
            return std::nullopt;
        }
    }
    return dir_tree;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto DirectoryTree::AddArtifact(std::filesystem::path::iterator* begin,
                                std::filesystem::path::iterator const& end,
                                Artifact const* artifact) -> bool {
    ExpectsAudit(std::distance(*begin, end) > 0);
    auto segment = *((*begin)++);
    if (segment == "." or segment == "..") {  // fail on "." and ".."
        return false;
    }
    if (*begin == end) {
        return nodes_.emplace(segment, artifact).second;
    }
    auto const [it, success] =
        nodes_.emplace(segment, std::make_unique<DirectoryTree>());
    return (success or std::holds_alternative<DirectoryTreePtr>(it->second)) and
           std::get<DirectoryTreePtr>(it->second)
               ->AddArtifact(begin, end, artifact);
}
