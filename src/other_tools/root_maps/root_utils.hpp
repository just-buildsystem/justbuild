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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP

#include <optional>
#include <string>

#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"

/// \brief Calls the ServeApi to check whether the serve endpoint has the given
/// tree available to build against.
/// \param tree_id The Git-tree identifier.
/// \param logger An AsyncMapConsumer logger instance.
/// \returns Nullopt if an error in the ServeApi call ocurred, or a flag stating
/// whether the serve endpoint knows the tree on ServeApi call success. The
/// logger is called with fatal ONLY if this method returns nullopt.
[[nodiscard]] auto CheckServeHasAbsentRoot(
    ServeApi const& serve,
    std::string const& tree_id,
    AsyncMapConsumerLoggerPtr const& logger) -> std::optional<bool>;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP
