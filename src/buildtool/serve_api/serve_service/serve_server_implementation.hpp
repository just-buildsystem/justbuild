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

#ifndef SERVE_SERVER_IMPLEMENTATION_HPP
#define SERVE_SERVER_IMPLEMENTATION_HPP

#include <string>

#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"

class ServeServerImpl {
  public:
    ServeServerImpl() noexcept = default;
    [[nodiscard]] static auto Instance() noexcept -> ServeServerImpl& {
        static ServeServerImpl x;
        return x;
    }

    [[nodiscard]] static auto SetInterface(std::string const& x) noexcept
        -> bool {
        Instance().interface_ = x;
        return true;
    }

    [[nodiscard]] static auto SetPidFile(std::string const& x) noexcept -> bool;

    [[nodiscard]] static auto SetPort(int x) noexcept -> bool;

    [[nodiscard]] static auto SetInfoFile(std::string const& x) noexcept
        -> bool;

    ServeServerImpl(ServeServerImpl const&) = delete;
    auto operator=(ServeServerImpl const&) noexcept
        -> ServeServerImpl& = delete;

    ServeServerImpl(ServeServerImpl&&) noexcept = delete;
    auto operator=(ServeServerImpl&&) noexcept -> ServeServerImpl& = delete;

    /// \brief Start the serve service.
    /// \param serve_config RemoteServeConfig to be used.
    /// \param serve        ServeApi to be used.
    /// \param with_execute Flag specifying if just serve should act also as
    /// just execute (i.e., start remote execution services with same interface)
    auto Run(RemoteServeConfig const& serve_config,
             std::optional<ServeApi> const& serve,
             bool with_execute) -> bool;
    ~ServeServerImpl() = default;

  private:
    std::string interface_{"127.0.0.1"};
    int port_{0};
    std::string info_file_{};
    std::string pid_file_{};
};

#endif  // SERVE_SERVER_IMPLEMENTATION_HPP
