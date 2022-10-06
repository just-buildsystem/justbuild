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
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/local_storage.hpp"

class LocalApi;

/// \brief Action for local execution.
class LocalAction final : public IExecutionAction {
    friend class LocalApi;

  public:
    struct Output {
        bazel_re::ActionResult action{};
        bool is_cached{};
    };

    auto Execute(Logger const* logger) noexcept
        -> IExecutionResponse::Ptr final;

    void SetCacheFlag(CacheFlag flag) noexcept final { cache_flag_ = flag; }

    void SetTimeout(std::chrono::milliseconds timeout) noexcept final {
        timeout_ = timeout;
    }

  private:
    Logger logger_{"LocalExecution"};
    std::shared_ptr<LocalStorage> storage_;
    ArtifactDigest root_digest_{};
    std::vector<std::string> cmdline_{};
    std::vector<std::string> output_files_{};
    std::vector<std::string> output_dirs_{};
    std::map<std::string, std::string> env_vars_{};
    std::vector<bazel_re::Platform_Property> properties_;
    std::chrono::milliseconds timeout_{kDefaultTimeout};
    CacheFlag cache_flag_{CacheFlag::CacheOutput};

    LocalAction(std::shared_ptr<LocalStorage> storage,
                ArtifactDigest root_digest,
                std::vector<std::string> command,
                std::vector<std::string> output_files,
                std::vector<std::string> output_dirs,
                std::map<std::string, std::string> env_vars,
                std::map<std::string, std::string> const& properties) noexcept
        : storage_{std::move(storage)},
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

    [[nodiscard]] auto CollectOutputFile(std::filesystem::path const& exec_path,
                                         std::string const& local_path)
        const noexcept -> std::optional<bazel_re::OutputFile>;

    [[nodiscard]] auto CollectOutputDir(std::filesystem::path const& exec_path,
                                        std::string const& local_path)
        const noexcept -> std::optional<bazel_re::OutputDirectory>;

    [[nodiscard]] auto CollectAndStoreOutputs(
        bazel_re::ActionResult* result,
        std::filesystem::path const& exec_path) const noexcept -> bool;

    /// \brief Store file from path in file CAS and return pointer to digest.
    [[nodiscard]] auto DigestFromOwnedFile(
        std::filesystem::path const& file_path) const noexcept
        -> gsl::owner<bazel_re::Digest*>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_ACTION_HPP
