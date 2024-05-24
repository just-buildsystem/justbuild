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

#include "src/buildtool/execution_api/local/local_cas_reader.hpp"

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/path.hpp"

auto LocalCasReader::ReadDirectory(ArtifactDigest const& digest) const noexcept
    -> std::optional<bazel_re::Directory> {
    if (auto const path = cas_.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                *content);
        }
    }
    Logger::Log(
        LogLevel::Error, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto LocalCasReader::ReadGitTree(ArtifactDigest const& digest) const noexcept
    -> std::optional<GitRepo::tree_entries_t> {
    if (auto const path = cas_.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            auto check_symlinks =
                [this](std::vector<bazel_re::Digest> const& ids) {
                    for (auto const& id : ids) {
                        auto link_path = cas_.BlobPath(id,
                                                       /*is_executable=*/false);
                        if (not link_path) {
                            return false;
                        }
                        // in the local CAS we store as files
                        auto content = FileSystemManager::ReadFile(*link_path);
                        if (not content or not PathIsNonUpwards(*content)) {
                            return false;
                        }
                    }
                    return true;
                };
            return GitRepo::ReadTreeData(
                *content,
                HashFunction::ComputeTreeHash(*content).Bytes(),
                check_symlinks,
                /*is_hex_id=*/false);
        }
    }
    Logger::Log(LogLevel::Debug, "Tree {} not found in CAS", digest.hash());
    return std::nullopt;
}
