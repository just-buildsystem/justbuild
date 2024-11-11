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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/utils/cpp/expected.hpp"

struct RemoteExecutionConfig final {
    class Builder;

    // Server address of remote execution.
    std::optional<ServerAddress> const remote_address;

    // Server dispatch data
    std::vector<DispatchEndpoint> const dispatch;

    // Server address of cache endpoint for rebuild.
    std::optional<ServerAddress> const cache_address;

    // Platform properties for execution.
    ExecutionProperties const platform_properties;
};

class RemoteExecutionConfig::Builder final {
  public:
    // Set remote execution and cache address. If it fails to parse during
    // config build, will reset the fields.
    auto SetRemoteAddress(std::optional<std::string> address) noexcept
        -> Builder& {
        remote_address_raw_ = std::move(address);
        return *this;
    }

    // Set remote-execution dispatch property list filename.
    auto SetRemoteExecutionDispatch(
        std::optional<std::filesystem::path> filename) noexcept -> Builder& {
        dispatch_file_ = std::move(filename);
        return *this;
    }

    // Set specific cache address. If it fails to parse during config build,
    // will reset the field.
    auto SetCacheAddress(std::optional<std::string> address) noexcept
        -> Builder& {
        cache_address_raw_ = std::move(address);
        return *this;
    }

    // Set platform properties given as "key:val" strings.
    auto SetPlatformProperties(std::vector<std::string> properties) noexcept
        -> Builder& {
        platform_properties_raw_ = std::move(properties);
        return *this;
    }

    /// \brief Parse the set data to finalize creation of RemoteExecutionConfig.
    /// \return RemoteExecutionConfig on success, an error string on failure.
    [[nodiscard]] auto Build() const noexcept
        -> expected<RemoteExecutionConfig, std::string>;

  private:
    // Server address of remote execution; needs parsing.
    std::optional<std::string> remote_address_raw_;

    // Server dispatch data file; needs parsing.
    std::optional<std::filesystem::path> dispatch_file_;

    // Server address of cache endpoint for rebuild; needs parsing.
    std::optional<std::string> cache_address_raw_;

    // Platform properties for execution; needs parsing.
    std::vector<std::string> platform_properties_raw_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
