// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_FS_RESOLVE_SPECIAL_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_FS_RESOLVE_SPECIAL_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

/* Enum used by add-to-cas subcommand */

/// \brief Resolve special option value enum
enum class ResolveSpecial : std::uint8_t {
    // Ignore special entries
    Ignore,
    // Allow symlinks confined to tree and resolve the ones that are upwards
    TreeUpwards,
    // Allow symlinks confined to tree and resolve them
    TreeAll,
    // Unconditionally allow symlinks and resolve them
    All
};

/// \brief ResolveSpecial value map, from string to enum
inline std::unordered_map<std::string, ResolveSpecial> const
    kResolveSpecialMap = {{"ignore", ResolveSpecial::Ignore},
                          {"tree-upwards", ResolveSpecial::TreeUpwards},
                          {"tree-all", ResolveSpecial::TreeAll},
                          {"all", ResolveSpecial::All}};

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_SYMLINKS_FS_RESOLVE_SPECIAL_HPP
