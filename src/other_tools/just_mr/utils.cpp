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

namespace JustMR::Utils {

// NOLINTNEXTLINE(misc-no-recursion)
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
    return ResolveRepo(repos[desc_str]["repository"], repos, seen);
}

auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos) noexcept
    -> std::optional<ExpressionPtr> {
    std::unordered_set<std::string> seen = {};
    try {
        return ResolveRepo(repo_desc, repos, &seen);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Config: While resolving dependencies: {}",
                    e.what());
        return std::nullopt;
    }
}

}  // namespace JustMR::Utils
