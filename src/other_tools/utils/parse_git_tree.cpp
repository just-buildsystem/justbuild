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

#include "src/other_tools/utils/parse_git_tree.hpp"

#include <map>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"

[[nodiscard]] auto ParseGitTree(ExpressionPtr const& repo_desc,
                                std::optional<std::string> origin)
    -> expected<GitTreeInfo, std::string> {
    auto repo_desc_hash = repo_desc->At("id");
    if (not repo_desc_hash) {
        return unexpected<std::string>{"Mandatory field \"id\" is missing"};
    }
    if (not repo_desc_hash->get()->IsString()) {
        return unexpected{
            fmt::format("Unsupported value {} for "
                        "mandatory field \"id\"",
                        repo_desc_hash->get()->ToString())};
    }

    auto repo_desc_cmd = repo_desc->At("cmd");
    if (not repo_desc_cmd) {
        return unexpected<std::string>{"Mandatory field \"cmd\" is missing"};
    }
    if (not repo_desc_cmd->get()->IsList()) {
        return unexpected{
            fmt::format("Unsupported value {} for "
                        "mandatory field \"cmd\"",
                        repo_desc_cmd->get()->ToString())};
    }
    std::vector<std::string> cmd{};
    for (auto const& token : repo_desc_cmd->get()->List()) {
        if (token.IsNotNull() and token->IsString()) {
            cmd.emplace_back(token->String());
        }
        else {
            return unexpected{
                fmt::format("Unsupported entry {} "
                            "in mandatory field \"cmd\"",
                            token->ToString())};
        }
    }
    std::map<std::string, std::string> env{};
    auto repo_desc_env = repo_desc->Get("env", Expression::none_t{});
    if (repo_desc_env.IsNotNull() and repo_desc_env->IsMap()) {
        for (auto const& envar : repo_desc_env->Map().Items()) {
            if (envar.second.IsNotNull() and envar.second->IsString()) {
                env.insert({envar.first, envar.second->String()});
            }
            else {
                return unexpected{
                    fmt::format("Unsupported value {} for "
                                "key {} in optional field \"envs\"",
                                envar.second->ToString(),
                                nlohmann::json(envar.first).dump())};
            }
        }
    }
    std::vector<std::string> inherit_env{};
    auto repo_desc_inherit_env =
        repo_desc->Get("inherit env", Expression::none_t{});
    if (repo_desc_inherit_env.IsNotNull() and repo_desc_inherit_env->IsList()) {
        for (auto const& envvar : repo_desc_inherit_env->List()) {
            if (envvar->IsString()) {
                inherit_env.emplace_back(envvar->String());
            }
            else {
                return unexpected{
                    fmt::format("Not a variable "
                                "name in the specification "
                                "of \"inherit env\": {}",
                                envvar->ToString())};
            }
        }
    }
    // populate struct
    auto info = GitTreeInfo{.hash = repo_desc_hash->get()->String(),
                            .env_vars = std::move(env),
                            .inherit_env = std::move(inherit_env),
                            .command = std::move(cmd)};
    if (origin) {
        info.origin = *std::move(origin);
    }
    return info;
}
