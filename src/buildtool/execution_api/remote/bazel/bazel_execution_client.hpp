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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_EXECUTION_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_EXECUTION_CLIENT_HPP

#include <memory>
#include <optional>
#include <string>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for serivce defined here:
/// https://github.com/bazelbuild/bazel/blob/4b6ad34dbba15dacebfb6cbf76fa741649cdb007/third_party/remoteapis/build/bazel/remote/execution/v2/remote_execution.proto#L42
class BazelExecutionClient {
  public:
    struct ExecutionOutput {
        bazel_re::ActionResult action_result{};
        bool cached_result{};
        grpc::Status status{};
        // TODO(vmoreno): switch to non-google type for the map
        google::protobuf::Map<std::string, bazel_re::LogFile> server_logs{};
        std::string message{};
    };

    struct ExecutionResponse {
        enum class State { Failed, Ongoing, Finished, Unknown };

        std::string execution_handle{};
        State state{State::Unknown};
        std::optional<ExecutionOutput> output{std::nullopt};

        static auto MakeEmptyFailed() -> ExecutionResponse {
            return ExecutionResponse{
                {}, ExecutionResponse::State::Failed, std::nullopt};
        }
    };

    BazelExecutionClient(std::string const& server,
                         Port port,
                         std::string const& user = "",
                         std::string const& pwd = "") noexcept;

    [[nodiscard]] auto Execute(std::string const& instance_name,
                               bazel_re::Digest const& action_digest,
                               ExecutionConfiguration const& config,
                               bool wait) -> ExecutionResponse;

    [[nodiscard]] auto WaitExecution(std::string const& execution_handle)
        -> ExecutionResponse;

  private:
    std::unique_ptr<bazel_re::Execution::Stub> stub_;
    Logger logger_{"RemoteExecutionClient"};

    [[nodiscard]] auto ReadExecution(
        grpc::ClientReader<google::longrunning::Operation>* reader,
        bool wait) -> std::optional<google::longrunning::Operation>;

    [[nodiscard]] auto ExtractContents(
        std::optional<google::longrunning::Operation>&& operation)
        -> ExecutionResponse;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_EXECUTION_CLIENT_HPP
