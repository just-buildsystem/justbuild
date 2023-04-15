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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"

/// \brief Abstract response.
/// Response of an action execution. Contains outputs from multiple commands and
/// a single container with artifacts.
class IExecutionResponse {
  public:
    using Ptr = std::unique_ptr<IExecutionResponse>;
    using ArtifactInfos = std::unordered_map<std::string, Artifact::ObjectInfo>;

    enum class StatusCode { Failed, Success };

    IExecutionResponse() = default;
    IExecutionResponse(IExecutionResponse const&) = delete;
    IExecutionResponse(IExecutionResponse&&) = delete;
    auto operator=(IExecutionResponse const&) -> IExecutionResponse& = delete;
    auto operator=(IExecutionResponse&&) -> IExecutionResponse& = delete;
    virtual ~IExecutionResponse() = default;

    [[nodiscard]] virtual auto Status() const noexcept -> StatusCode = 0;

    [[nodiscard]] virtual auto ExitCode() const noexcept -> int = 0;

    [[nodiscard]] virtual auto IsCached() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto HasStdErr() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto HasStdOut() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto StdErr() noexcept -> std::string = 0;

    [[nodiscard]] virtual auto StdOut() noexcept -> std::string = 0;

    [[nodiscard]] virtual auto ActionDigest() const noexcept -> std::string = 0;

    [[nodiscard]] virtual auto Artifacts() const noexcept -> ArtifactInfos = 0;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP
