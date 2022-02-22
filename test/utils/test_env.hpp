#ifndef INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
#define INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP

#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include "test/utils/logging/log_config.hpp"

[[nodiscard]] static inline auto ReadPlatformPropertiesFromEnv()
    -> std::map<std::string, std::string> {
    std::map<std::string, std::string> properties{};
    auto* execution_props = std::getenv("REMOTE_EXECUTION_PROPERTIES");
    if (execution_props not_eq nullptr) {
        std::istringstream pss(std::string{execution_props});
        std::string keyval_pair;
        while (std::getline(pss, keyval_pair, ';')) {
            std::istringstream kvss{keyval_pair};
            std::string key;
            std::string val;
            if (not std::getline(kvss, key, ':') or
                not std::getline(kvss, val, ':')) {
                Logger::Log(LogLevel::Error,
                            "parsing property '{}' failed.",
                            keyval_pair);
                std::exit(EXIT_FAILURE);
            }
            properties.emplace(std::move(key), std::move(val));
        }
    }
    return properties;
}

[[nodiscard]] static inline auto ReadRemoteAddressFromEnv()
    -> std::optional<std::string> {
    auto* execution_address = std::getenv("REMOTE_EXECUTION_ADDRESS");
    return execution_address == nullptr
               ? std::nullopt
               : std::make_optional(std::string{execution_address});
}

#endif  // INCLUDED_SRC_TEST_UTILS_TEST_ENV_HPP
