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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

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

    using FileCopies = std::unordered_map<Artifact::ObjectInfo, TmpDirPtr>;

    auto Execute(Logger const* logger) noexcept
        -> IExecutionResponse::Ptr final;

    void SetCacheFlag(CacheFlag flag) noexcept final { cache_flag_ = flag; }

    void SetTimeout(std::chrono::milliseconds timeout) noexcept final {
        timeout_ = timeout;
    }

  private:
    Logger logger_{"LocalExecution"};
    StorageConfig const& storage_config_;
    Storage const& storage_;
    LocalExecutionConfig const& exec_config_;
    ArtifactDigest const root_digest_{};
    std::vector<std::string> const cmdline_{};
    std::vector<std::string> output_files_{};
    std::vector<std::string> output_dirs_{};
    std::map<std::string, std::string> const env_vars_{};
    std::vector<bazel_re::Platform_Property> const properties_;
    std::chrono::milliseconds timeout_{kDefaultTimeout};
    CacheFlag cache_flag_{CacheFlag::CacheOutput};

    explicit LocalAction(
        gsl::not_null<StorageConfig const*> storage_config,
        gsl::not_null<Storage const*> const& storage,
        gsl::not_null<LocalExecutionConfig const*> const& exec_config,
        ArtifactDigest root_digest,
        std::vector<std::string> command,
        std::vector<std::string> output_files,
        std::vector<std::string> output_dirs,
        std::map<std::string, std::string> env_vars,
        std::map<std::string, std::string> const& properties) noexcept
        : storage_config_{*storage_config},
          storage_{*storage},
          exec_config_{*exec_config},
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
        -> std::optional<bazel_re::Digest> {
        auto const env_vars = BazelMsgFactory::CreateMessageVectorFromMap<
            bazel_re::Command_EnvironmentVariable>(env_vars_);

        BazelMsgFactory::ActionDigestRequest request{
            .command_line = &cmdline_,
            .output_files = &output_files_,
            .output_dirs = &output_dirs_,
            .env_vars = &env_vars,
            .properties = &properties_,
            .exec_dir = &exec_dir,
            .hash_function = storage_config_.hash_function,
            .timeout = timeout_,
            .skip_action_cache = do_not_cache};
        return BazelMsgFactory::CreateActionDigestFromCommandLine(request);
    }

    [[nodiscard]] auto Run(bazel_re::Digest const& action_id) const noexcept
        -> std::optional<Output>;

    [[nodiscard]] auto StageInput(
        std::filesystem::path const& target_path,
        Artifact::ObjectInfo const& info,
        gsl::not_null<FileCopies*> copies) const noexcept -> bool;

    /// \brief Stage input artifacts and leaf trees to the execution directory.
    /// Stage artifacts and their parent directory structure from CAS to the
    /// specified execution directory. The execution directory may no exist.
    /// \param[in] exec_path Absolute path to the execution directory.
    /// \returns Success indicator.
    [[nodiscard]] auto StageInputs(
        std::filesystem::path const& exec_path,
        gsl::not_null<FileCopies*> copies) const noexcept -> bool;

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
