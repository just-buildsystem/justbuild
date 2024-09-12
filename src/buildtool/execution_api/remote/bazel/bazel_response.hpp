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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP

#include <optional>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/utils/cpp/expected.hpp"

class BazelAction;

/// \brief Bazel implementation of the abstract Execution Response.
/// Access Bazel execution output data and obtain a Bazel Artifact.
class BazelResponse final : public IExecutionResponse {
    friend class BazelAction;

  public:
    auto Status() const noexcept -> StatusCode final {
        return output_.status.ok() ? StatusCode::Success : StatusCode::Failed;
    }
    auto HasStdErr() const noexcept -> bool final {
        return IsDigestNotEmpty(output_.action_result.stderr_digest());
    }
    auto HasStdOut() const noexcept -> bool final {
        return IsDigestNotEmpty(output_.action_result.stdout_digest());
    }
    auto StdErr() noexcept -> std::string final {
        return ReadStringBlob(output_.action_result.stderr_digest());
    }
    auto StdOut() noexcept -> std::string final {
        return ReadStringBlob(output_.action_result.stdout_digest());
    }
    auto ExitCode() const noexcept -> int final {
        return output_.action_result.exit_code();
    }
    auto IsCached() const noexcept -> bool final {
        return output_.cached_result;
    };

    auto ActionDigest() const noexcept -> std::string const& final {
        return action_id_;
    }

    auto Artifacts() noexcept
        -> expected<gsl::not_null<ArtifactInfos const*>, std::string> final;
    auto DirectorySymlinks() noexcept
        -> expected<gsl::not_null<DirSymlinks const*>, std::string> final;

  private:
    std::string action_id_{};
    std::shared_ptr<BazelNetwork> const network_{};
    BazelExecutionClient::ExecutionOutput output_{};
    ArtifactInfos artifacts_;
    DirSymlinks dir_symlinks_;
    bool populated_ = false;

    explicit BazelResponse(std::string action_id,
                           std::shared_ptr<BazelNetwork> network,
                           BazelExecutionClient::ExecutionOutput output)
        : action_id_{std::move(action_id)},
          network_{std::move(network)},
          output_{std::move(output)} {}

    [[nodiscard]] auto ReadStringBlob(bazel_re::Digest const& id) noexcept
        -> std::string;

    [[nodiscard]] static auto IsDigestNotEmpty(bazel_re::Digest const& id)
        -> bool {
        return id.size_bytes() != 0;
    }

    /// \brief Populates the stored data, once.
    /// \returns Error message on failure, nullopt on success.
    [[nodiscard]] auto Populate() noexcept -> std::optional<std::string>;

    [[nodiscard]] auto UploadTreeMessageDirectories(
        bazel_re::Tree const& tree) const -> std::optional<ArtifactDigest>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
