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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_ACTION_HPP

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/storage/storage.hpp"

class LocalApi;

/// \brief Action for local execution.
class LocalAction final : public IExecutionAction {
    friend class LocalApi;

  public:
    struct Output {
        bazel_re::ActionResult action{};
        bool is_cached{};
    };

    using OutputFileOrSymlink =
        std::variant<bazel_re::OutputFile, bazel_re::OutputSymlink>;
    using OutputDirOrSymlink =
        std::variant<bazel_re::OutputDirectory, bazel_re::OutputSymlink>;

    auto Execute(Logger const* logger) noexcept
        -> IExecutionResponse::Ptr final;

    void SetCacheFlag(CacheFlag flag) noexcept final { cache_flag_ = flag; }

    void SetTimeout(std::chrono::milliseconds timeout) noexcept final {
        timeout_ = timeout;
    }

  private:
    Logger logger_{"LocalExecution"};
    gsl::not_null<Storage const*> storage_;
    ArtifactDigest root_digest_{};
    std::vector<std::string> cmdline_{};
    std::vector<std::string> output_files_{};
    std::vector<std::string> output_dirs_{};
    std::map<std::string, std::string> env_vars_{};
    std::vector<bazel_re::Platform_Property> properties_;
    std::chrono::milliseconds timeout_{kDefaultTimeout};
    CacheFlag cache_flag_{CacheFlag::CacheOutput};

    LocalAction(gsl::not_null<Storage const*> const& storage,
                ArtifactDigest root_digest,
                std::vector<std::string> command,
                std::vector<std::string> output_files,
                std::vector<std::string> output_dirs,
                std::map<std::string, std::string> env_vars,
                std::map<std::string, std::string> const& properties) noexcept
        : storage_{storage},
          root_digest_{std::move(root_digest)},
          cmdline_{std::move(command)},
          output_files_{std::move(output_files)},
          output_dirs_{std::move(output_dirs)},
          env_vars_{std::move(env_vars)},
          properties_{BazelMsgFactory::CreateMessageVectorFromMap<
              bazel_re::Platform_Property>(properties)} {
        std::sort(output_files_.begin(), output_files_.end());
        std::sort(output_dirs_.begin(), output_dirs_.end());
    }

    [[nodiscard]] auto CreateActionDigest(bazel_re::Digest const& exec_dir,
                                          bool do_not_cache)
        -> bazel_re::Digest {
        return BazelMsgFactory::CreateActionDigestFromCommandLine(
            cmdline_,
            exec_dir,
            output_files_,
            output_dirs_,
            {} /*FIXME output node properties*/,
            BazelMsgFactory::CreateMessageVectorFromMap<
                bazel_re::Command_EnvironmentVariable>(env_vars_),
            properties_,
            do_not_cache,
            timeout_);
    }

    [[nodiscard]] auto Run(bazel_re::Digest const& action_id) const noexcept
        -> std::optional<Output>;

    [[nodiscard]] auto StageFile(std::filesystem::path const& target_path,
                                 Artifact::ObjectInfo const& info) const
        -> bool;

    /// \brief Stage input artifacts to the execution directory.
    /// Stage artifacts and their parent directory structure from CAS to the
    /// specified execution directory. The execution directory may no exist.
    /// \param[in] exec_path Absolute path to the execution directory.
    /// \returns Success indicator.
    [[nodiscard]] auto StageInputFiles(
        std::filesystem::path const& exec_path) const noexcept -> bool;

    [[nodiscard]] auto CreateDirectoryStructure(
        std::filesystem::path const& exec_path) const noexcept -> bool;

    [[nodiscard]] auto CollectOutputFileOrSymlink(
        std::filesystem::path const& exec_path,
        std::string const& local_path) const noexcept
        -> std::optional<OutputFileOrSymlink>;

    [[nodiscard]] auto CollectOutputDirOrSymlink(
        std::filesystem::path const& exec_path,
        std::string const& local_path) const noexcept
        -> std::optional<OutputDirOrSymlink>;

    [[nodiscard]] auto CollectAndStoreOutputs(
        bazel_re::ActionResult* result,
        std::filesystem::path const& exec_path) const noexcept -> bool;

    /// \brief Store file from path in file CAS and return pointer to digest.
    [[nodiscard]] auto DigestFromOwnedFile(
        std::filesystem::path const& file_path) const noexcept
        -> gsl::owner<bazel_re::Digest*>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_ACTION_HPP
