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

#include "src/other_tools/root_maps/root_utils.hpp"

#include <functional>
#include <memory>

#include "fmt/core.h"

auto CheckServeHasAbsentRoot(ServeApi const& serve,
                             std::string const& tree_id,
                             AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<bool> {
    if (auto has_tree = serve.CheckRootTree(tree_id)) {
        return has_tree;
    }
    (*logger)(fmt::format("Checking that the serve endpoint knows tree "
                          "{} failed.",
                          tree_id),
              /*fatal=*/true);
    return std::nullopt;
}
