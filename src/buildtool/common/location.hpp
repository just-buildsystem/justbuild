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

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/utils/cpp/expected.hpp"

using location_res_t = std::pair<std::filesystem::path, std::filesystem::path>;

/// \brief Parse a location object stored in a JSON object.
/// \returns optional parsed location (nullopt if location should be ignored) on
/// success or unexpected error as string.
[[nodiscard]] auto ReadLocationObject(
    nlohmann::json const& location,
    std::optional<std::filesystem::path> const& ws_root)
    -> expected<std::optional<location_res_t>, std::string>;
