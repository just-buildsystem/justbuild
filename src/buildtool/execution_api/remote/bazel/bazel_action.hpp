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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"

class BazelApi;

/// \brief Bazel implementation of the abstract Execution Action.
/// Uploads all dependencies, creates a Bazel Action and executes it.
class BazelAction final : public IExecutionAction {
    friend class BazelApi;

  public:
    auto Execute(Logger const* logger) noexcept
        -> IExecutionResponse::Ptr final;
    void SetCacheFlag(CacheFlag flag) noexcept final { cache_flag_ = flag; }
    void SetTimeout(std::chrono::milliseconds timeout) noexcept final {
        timeout_ = timeout;
    }

  private:
    std::shared_ptr<BazelNetwork> const network_;
    bazel_re::Digest const root_digest_;
    std::vector<std::string> const cmdline_;
    std::vector<std::string> output_files_;
    std::vector<std::string> output_dirs_;
    std::vector<bazel_re::Command_EnvironmentVariable> const env_vars_;
    std::vector<bazel_re::Platform_Property> const properties_;
    CacheFlag cache_flag_{CacheFlag::CacheOutput};
    std::chrono::milliseconds timeout_{kDefaultTimeout};

    explicit BazelAction(
        std::shared_ptr<BazelNetwork> network,
        bazel_re::Digest root_digest,
        std::vector<std::string> command,
        std::vector<std::string> output_files,
        std::vector<std::string> output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) noexcept;

    [[nodiscard]] auto CreateBundlesForAction(BazelBlobContainer* blobs,
                                              bazel_re::Digest const& exec_dir,
                                              bool do_not_cache) const noexcept
        -> std::optional<bazel_re::Digest>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP
