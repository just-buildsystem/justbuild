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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP

#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief String representation of the used execution backend.
[[nodiscard]] auto DescribeBackend(
    std::optional<ServerAddress> const& address,
    ExecutionProperties const& properties,
    std::vector<DispatchEndpoint> const& dispatch) noexcept
    -> expected<std::string, std::string>;

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP
