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

#ifndef INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPS_TYPES_HPP
#define INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPS_TYPES_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>  // std::move

#include "src/buildtool/file_system/git_cas.hpp"
#include "src/utils/cpp/path.hpp"

/// \brief Common parameters for all critical Git operations
struct GitOpParams {
    std::filesystem::path target_path{}; /*key*/
    std::string git_hash{};              /*key*/
    std::string branch{};                /*key*/
    std::optional<std::string> message{
        std::nullopt};                            // useful for commits and tags
    std::optional<bool> init_bare{std::nullopt};  // useful for git init

    GitOpParams(std::filesystem::path const& target_path_,
                std::string git_hash_,
                std::string branch_,
                std::optional<std::string> message_ = std::nullopt,
                std::optional<bool> init_bare_ = std::nullopt)
        : target_path{std::filesystem::absolute(ToNormalPath(target_path_))},
          git_hash{std::move(git_hash_)},
          branch{std::move(branch_)},
          message{std::move(message_)},
          init_bare{init_bare_} {};

    [[nodiscard]] auto operator==(GitOpParams const& other) const noexcept
        -> bool {
        // not all fields are keys
        return target_path == other.target_path && git_hash == other.git_hash &&
               branch == other.branch;
    }
};

/// \brief Defines the type of Git operation
enum class GitOpType {
    DEFAULT_OP,  // default value; does nothing
    INITIAL_COMMIT,
    ENSURE_INIT,
    KEEP_TAG,
    GET_HEAD_ID
};

/// \brief Common return value for all critical Git operations
struct GitOpValue {
    // used to continue with non-critical ops on same odb, if needed
    GitCASPtr git_cas{nullptr};
    // stores result of certain operations; always nullopt if failure
    std::optional<std::string> result{""};
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_OPS_TYPES_HPP
