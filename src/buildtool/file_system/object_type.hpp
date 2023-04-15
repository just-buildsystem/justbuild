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
};

[[nodiscard]] constexpr auto FromChar(char c) -> ObjectType {
    switch (c) {
        case 'x':
            return ObjectType::Executable;
        case 't':
            return ObjectType::Tree;
        default:
            return ObjectType::File;
    }
    Ensures(false);  // unreachable
}

[[nodiscard]] constexpr auto ToChar(ObjectType type) -> char {
    switch (type) {
        case ObjectType::File:
            return 'f';
        case ObjectType::Executable:
            return 'x';
        case ObjectType::Tree:
            return 't';
    }
    Ensures(false);  // unreachable
}

[[nodiscard]] constexpr auto IsFileObject(ObjectType type) -> bool {
    return type == ObjectType::Executable or type == ObjectType::File;
}

[[nodiscard]] constexpr auto IsExecutableObject(ObjectType type) -> bool {
    return type == ObjectType::Executable;
}

[[nodiscard]] constexpr auto IsTreeObject(ObjectType type) -> bool {
    return type == ObjectType::Tree;
}

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_OBJECT_TYPE_HPP
