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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_CONFIGURATION_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_CONFIGURATION_CLIENT_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for Configuration service defined in:
/// src/buildtool/serve_api/serve_service/just_serve.proto
class ConfigurationClient {
  public:
    ConfigurationClient(std::string const& server, Port port) noexcept
        : stub_{justbuild::just_serve::Configuration::NewStub(
              CreateChannelWithCredentials(server, port))} {}

    auto CheckServeRemoteExecution() -> bool;

  private:
    std::unique_ptr<justbuild::just_serve::Configuration::Stub> stub_;
    Logger logger_{"RemoteConfigurationClient"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_CONFIGURATION_CLIENT_HPP
