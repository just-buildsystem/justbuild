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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_UTILS_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/git_repo.hpp"

class TreeReaderUtils final {
  public:
    using InfoStoreFunc = std::function<bool(std::filesystem::path const&,
                                             Artifact::ObjectInfo const&)>;

    /// \brief Read object infos from directory.
    /// \returns true on success.
    [[nodiscard]] static auto ReadObjectInfos(
        bazel_re::Directory const& dir,
        InfoStoreFunc const& store_info) noexcept -> bool;

    /// \brief Read object infos from git tree.
    /// \returns true on success.
    [[nodiscard]] static auto ReadObjectInfos(
        GitRepo::tree_entries_t const& entries,
        InfoStoreFunc const& store_info) noexcept -> bool;

    /// \brief Create descriptive string from Directory protobuf message.
    [[nodiscard]] static auto DirectoryToString(
        bazel_re::Directory const& dir) noexcept -> std::optional<std::string>;

    /// \brief Create descriptive string from Git tree entries.
    [[nodiscard]] static auto GitTreeToString(
        GitRepo::tree_entries_t const& entries) noexcept
        -> std::optional<std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_TREE_READER_UTILS_HPP
