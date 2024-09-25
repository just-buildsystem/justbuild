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

#ifndef INCLUDED_SRC_TEST_UTILS_LARGE_OBJECTS_LARGE_OBJECT_UTILS_HPP
#define INCLUDED_SRC_TEST_UTILS_LARGE_OBJECTS_LARGE_OBJECT_UTILS_HPP

#include <cstdint>
#include <filesystem>
#include <string_view>

/// \brief Provides an interface for randomizing large files and directories.
class LargeObjectUtils {
  public:
    static constexpr std::string_view kTreeEntryPrefix =
        "additional-large-prefix-to-make-tree-entry-larger";

    /// \brief Generate a file of the specified size in the specified location.
    /// If the file exists, it is overwritten. To reduce the number of
    /// randomizations, a pool of pre-generated chunks is used.
    /// \param path             Output path.
    /// \param size             Size of the resulting file in bytes.
    /// \param is_executable    Set executable permissions
    /// \return                 True if the file is generated properly.
    [[nodiscard]] static auto GenerateFile(std::filesystem::path const& path,
                                           std::uintmax_t size,
                                           bool is_executable = false) noexcept
        -> bool;

    /// \brief Generate a directory in the specified location and fill it with a
    /// number of randomized files. If the directory exists, it is overwritten.
    /// The name of each file contains a random number and is prefixed with
    /// kTreeEntryPrefix (to make tree entry larger for git). Each file contains
    /// the same random number as in it's name.
    /// \param path             Output path.
    /// \param entries_count    Number of file entries in the directory.
    /// \return                 True if the directory is generated properly.
    [[nodiscard]] static auto GenerateDirectory(
        std::filesystem::path const& path,
        std::uintmax_t entries_count) noexcept -> bool;
};

#endif  // INCLUDED_SRC_TEST_UTILS_LARGE_OBJECTS_LARGE_OBJECT_UTILS_HPP
