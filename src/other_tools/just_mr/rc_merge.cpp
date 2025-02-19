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

#include "src/other_tools/just_mr/rc_merge.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"

namespace {

auto const kAccumulating = std::vector<std::string>{"distdirs"};
auto const kLocalMerge =
    std::vector<std::string>{"just args", "just files", "invocation log"};

}  // namespace

[[nodiscard]] auto MergeMRRC(const Configuration& base,
                             const Configuration& delta) noexcept
    -> Configuration {
    // For most fields, just let the delta entry override
    auto result = base.Update(delta.Expr());

    // Accumulating fields
    for (auto const& f : kAccumulating) {
        auto joined = Expression::list_t{};
        auto const& deltaf = delta[f];
        if (deltaf->IsList()) {
            std::copy(deltaf->List().begin(),
                      deltaf->List().end(),
                      std::back_inserter(joined));
        }
        auto const& basef = base[f];
        if (basef->IsList()) {
            std::copy(basef->List().begin(),
                      basef->List().end(),
                      std::back_inserter(joined));
        }
        result = result.Update(
            ExpressionPtr{Expression::map_t{f, ExpressionPtr{joined}}});
    }

    // Locally-merging fields
    for (auto const& f : kLocalMerge) {
        auto joined = Configuration{Expression::kEmptyMap};
        auto const& basef = base[f];
        if (basef->IsMap()) {
            joined = joined.Update(basef);
        }
        auto const& deltaf = delta[f];
        if (deltaf->IsMap()) {
            joined = joined.Update(deltaf);
        }
        result =
            result.Update(ExpressionPtr{Expression::map_t{f, joined.Expr()}});
    }

    return result;
}
