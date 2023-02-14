// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_CONFIG_SETTINGS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_CONFIG_SETTINGS_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>

extern "C" {
struct git_cert;
struct git_config;
using git_transport_certificate_check_cb = auto (*)(git_cert*,
                                                    int,
                                                    const char*,
                                                    void*) -> int;
}

// Internal type used for logging with AsyncMaps
using anon_logger_t = std::function<void(std::string const&, bool)>;
using anon_logger_ptr = std::shared_ptr<anon_logger_t>;

/// \brief Contains the proxy URL if proxy is set, or nullopt if proxy unset.
using ProxyInfo = std::optional<std::string>;

namespace GitConfigSettings {

/// \brief Get a custom SSL certificate check callback to honor the existing
/// Git configuration of a repository trying to connect to a remote.
/// A null config snapshot reference will simply be ignored.
/// Returns nullopt if errors.
[[nodiscard]] auto GetSSLCallback(std::shared_ptr<git_config> const& cfg,
                                  std::string const& url,
                                  anon_logger_ptr const& logger)
    -> std::optional<git_transport_certificate_check_cb>;

/// \brief Get the remote proxy settings from envariables and the given git
/// config snapshot. Performs same checks and honors same settings as git.
/// Returns the proxy state and information, or nullopt if errors.
[[nodiscard]] auto GetProxySettings(std::shared_ptr<git_config> const& cfg,
                                    std::string const& url,
                                    anon_logger_ptr const& logger)
    -> std::optional<ProxyInfo>;

}  // namespace GitConfigSettings

#endif  // INCLUDED_SRC_OTHER_TOOLS_GIT_OPERATIONS_GIT_CONFIG_SETTINGS_HPP
