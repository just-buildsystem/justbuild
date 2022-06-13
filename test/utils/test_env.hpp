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
