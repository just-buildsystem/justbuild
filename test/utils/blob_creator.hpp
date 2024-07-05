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

#ifndef INCLUDED_SRC_TEST_UTILS_BLOB_CREATOR_HPP
#define INCLUDED_SRC_TEST_UTILS_BLOB_CREATOR_HPP

#include <filesystem>
#include <optional>
#include <string>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"

/// \brief Create a blob from the content found in file or symlink pointed to by
/// given path.
[[nodiscard]] static inline auto CreateBlobFromPath(
    std::filesystem::path const& fpath,
    HashFunction hash_function) noexcept -> std::optional<BazelBlob> {
    auto const type = FileSystemManager::Type(fpath, /*allow_upwards=*/true);
    if (not type) {
        return std::nullopt;
    }
    auto const content = FileSystemManager::ReadContentAtPath(fpath, *type);
    if (not content.has_value()) {
        return std::nullopt;
    }
    return BazelBlob{
        ArtifactDigest::Create<ObjectType::File>(hash_function, *content),
        *content,
        IsExecutableObject(*type)};
}

#endif  // INCLUDED_SRC_TEST_UTILS_BLOB_CREATOR_HPP
