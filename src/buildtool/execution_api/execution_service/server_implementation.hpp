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

#ifndef SERVER_IMPLEMENATION_HPP
#define SERVER_IMPLEMENATION_HPP

#include <fstream>
#include <string>

#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

class ServerImpl {
  public:
    ServerImpl() noexcept = default;
    [[nodiscard]] static auto Instance() noexcept -> ServerImpl& {
        static ServerImpl x;
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

    ServerImpl(ServerImpl const&) = delete;
    auto operator=(ServerImpl const&) noexcept -> ServerImpl& = delete;

    ServerImpl(ServerImpl&&) noexcept = delete;
    auto operator=(ServerImpl&&) noexcept -> ServerImpl& = delete;

    auto Run(StorageConfig const& storage_config,
             Storage const& storage,
             ApiBundle const& apis) -> bool;
    ~ServerImpl() = default;

  private:
    std::string interface_{"127.0.0.1"};
    int port_{0};
    std::string info_file_{};
    std::string pid_file_{};
};

#endif
