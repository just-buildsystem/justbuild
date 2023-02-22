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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_AC_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_AC_CLIENT_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for service defined here:
/// https://github.com/bazelbuild/bazel/blob/4b6ad34dbba15dacebfb6cbf76fa741649cdb007/third_party/remoteapis/build/bazel/remote/execution/v2/remote_execution.proto#L137
class BazelAcClient {
  public:
    BazelAcClient(std::string const& server, Port port) noexcept;

    [[nodiscard]] auto GetActionResult(
        std::string const& instance_name,
        bazel_re::Digest const& action_digest,
        bool inline_stdout,
        bool inline_stderr,
        std::vector<std::string> const& inline_output_files) noexcept
        -> std::optional<bazel_re::ActionResult>;

    [[nodiscard]] auto UpdateActionResult(std::string const& instance_name,
                                          bazel_re::Digest const& digest,
                                          bazel_re::ActionResult const& result,
                                          int priority) noexcept
        -> std::optional<bazel_re::ActionResult>;

  private:
    std::unique_ptr<bazel_re::ActionCache::Stub> stub_;
    Logger logger_{"RemoteAcClient"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_AC_CLIENT_HPP
