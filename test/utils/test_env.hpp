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

#ifndef INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
#define INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP

#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "src/buildtool/compatibility/compatibility.hpp"
#include "test/utils/logging/log_config.hpp"

[[nodiscard]] static inline auto ReadPlatformPropertiesFromEnv()
    -> std::vector<std::string> {
    std::vector<std::string> properties{};
    auto* execution_props = std::getenv("REMOTE_EXECUTION_PROPERTIES");
    if (execution_props not_eq nullptr) {
        std::istringstream pss(std::string{execution_props});
        std::string keyval_pair;
        while (std::getline(pss, keyval_pair, ';')) {
            properties.emplace_back(keyval_pair);
        }
    }
    return properties;
}

static inline void ReadCompatibilityFromEnv() {
    auto* compatible = std::getenv("COMPATIBLE");
    if (compatible != nullptr) {
        Compatibility::SetCompatible();
    }
}

[[nodiscard]] static inline auto ReadRemoteAddressFromEnv()
    -> std::optional<std::string> {
    auto* execution_address = std::getenv("REMOTE_EXECUTION_ADDRESS");
    return execution_address == nullptr
               ? std::nullopt
               : std::make_optional(std::string{execution_address});
}

#endif  // INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
