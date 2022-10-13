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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

struct BazelBlob {
    BazelBlob(bazel_re::Digest mydigest, std::string mydata)
        : digest{std::move(mydigest)}, data{std::move(mydata)} {}

    bazel_re::Digest digest{};
    std::string data{};
};

[[nodiscard]] static inline auto CreateBlobFromFile(
    std::filesystem::path const& file_path) noexcept
    -> std::optional<BazelBlob> {
    auto const content = FileSystemManager::ReadFile(file_path);
    if (not content.has_value()) {
        return std::nullopt;
    }
    return BazelBlob{ArtifactDigest::Create<ObjectType::File>(*content),
                     *content};
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
