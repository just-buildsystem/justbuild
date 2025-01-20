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

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/remote/context.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"

class ServeServerImpl final {
  public:
    [[nodiscard]] static auto Create(
        std::optional<std::string> interface,
        std::optional<int> port,
        std::optional<std::string> info_file,
        std::optional<std::string> pid_file,
        gsl::not_null<std::mutex*> const& lock) noexcept
        -> std::optional<ServeServerImpl>;

    ~ServeServerImpl() noexcept = default;

    ServeServerImpl(ServeServerImpl const&) = delete;
    auto operator=(ServeServerImpl const&) -> ServeServerImpl& = delete;

    ServeServerImpl(ServeServerImpl&&) noexcept = default;
    auto operator=(ServeServerImpl&&) noexcept -> ServeServerImpl& = default;

    /// \brief Start the serve service.
    /// \param serve_config     RemoteServeConfig to be used.
    /// \param local_context    Aggregate of storage and local configs to use.
    /// \param serve            ServeApi to be used.
    /// \param with_execute Flag specifying if just serve should act also as
    /// just execute (i.e., start remote execution services with same interface)
    auto Run(RemoteServeConfig const& serve_config,
             gsl::not_null<LocalContext const*> const& local_context,
             gsl::not_null<RemoteContext const*> const& remote_context,
             std::optional<ServeApi> const& serve,
             ApiBundle const& apis,
             std::optional<std::uint8_t> op_exponent,
             bool with_execute) -> bool;

  private:
    explicit ServeServerImpl(gsl::not_null<std::mutex*> lock) noexcept
        : lock_{lock} {};
    gsl::not_null<std::mutex*> lock_;

    std::string interface_{"127.0.0.1"};
    int port_{0};
    std::string info_file_;
    std::string pid_file_;
};

#endif  // SERVE_SERVER_IMPLEMENTATION_HPP
