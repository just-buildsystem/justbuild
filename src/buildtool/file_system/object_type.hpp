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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
#include <cstdint>

#include <gsl/gsl>

enum class ObjectType : std::int8_t {
    File,
    Executable,
    Tree,
    Symlink  // non-upwards symbolic link
};

[[nodiscard]] constexpr auto FromChar(char c) noexcept -> ObjectType {
    switch (c) {
        case 'x':
            return ObjectType::Executable;
        case 't':
            return ObjectType::Tree;
        case 'l':
            return ObjectType::Symlink;
        default:
            return ObjectType::File;
    }
    Ensures(false);  // unreachable
}

[[nodiscard]] constexpr auto ToChar(ObjectType type) noexcept -> char {
    switch (type) {
        case ObjectType::File:
            return 'f';
        case ObjectType::Executable:
            return 'x';
        case ObjectType::Tree:
            return 't';
        case ObjectType::Symlink:
            return 'l';
    }
    Ensures(false);  // unreachable
}

[[nodiscard]] constexpr auto IsFileObject(ObjectType type) noexcept -> bool {
    return type == ObjectType::Executable or type == ObjectType::File;
}

[[nodiscard]] constexpr auto IsExecutableObject(ObjectType type) noexcept
    -> bool {
    return type == ObjectType::Executable;
}

[[nodiscard]] constexpr auto IsTreeObject(ObjectType type) noexcept -> bool {
    return type == ObjectType::Tree;
}

/// \brief Non-upwards symlinks are designated as first-class citizens.
[[nodiscard]] constexpr auto IsSymlinkObject(ObjectType type) noexcept -> bool {
    return type == ObjectType::Symlink;
}

/// \brief Valid blob sources can be files, executables, or symlinks.
[[nodiscard]] constexpr auto IsBlobObject(ObjectType type) noexcept -> bool {
    return type == ObjectType::Executable or type == ObjectType::File or
           type == ObjectType::Symlink;
}

/// \brief Only regular files, executables, and trees are non-special entries.
[[nodiscard]] constexpr auto IsNonSpecialObject(ObjectType type) noexcept
    -> bool {
    return type == ObjectType::File or type == ObjectType::Executable or
           type == ObjectType::Tree;
}

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
