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

#include "src/other_tools/just_mr/utils.hpp"

#include <exception>

#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace JustMR::Utils {

auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos,
                 gsl::not_null<std::unordered_set<std::string>*> const& seen)
    -> std::optional<ExpressionPtr> {
    if (not repo_desc->IsString()) {
        return repo_desc;
    }
    auto desc_str = repo_desc->String();
    if (seen->contains(desc_str)) {
        // cyclic dependency
        return std::nullopt;
    }
    [[maybe_unused]] auto insert_res = seen->insert(desc_str);
    auto const& new_repo_desc = repos[desc_str];
    if (not new_repo_desc->IsMap()) {
        Logger::Log(LogLevel::Error,
                    "Config: While resolving dependencies:\nDescription of "
                    "repository {} is not a map",
                    desc_str);
        return std::nullopt;
    }
    if (not new_repo_desc->At("repository")) {
        Logger::Log(LogLevel::Error,
                    "Config: While resolving dependencies:\nKey \"repository\" "
                    "missing for repository {}",
                    desc_str);
        return std::nullopt;
    }
    return ResolveRepo(new_repo_desc->At("repository")->get(), repos, seen);
}

auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos) noexcept
    -> std::optional<ExpressionPtr> {
    std::unordered_set<std::string> seen = {};
    try {
        return ResolveRepo(repo_desc, repos, &seen);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Config: While resolving dependencies:\n{}",
                    e.what());
        return std::nullopt;
    }
}

}  // namespace JustMR::Utils
