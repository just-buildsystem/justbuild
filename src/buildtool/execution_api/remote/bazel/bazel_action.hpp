#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
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
    std::shared_ptr<BazelNetwork> network_;
    bazel_re::Digest const root_digest_;
    std::vector<std::string> const cmdline_;
    std::vector<std::string> output_files_;
    std::vector<std::string> output_dirs_;
    std::vector<bazel_re::Command_EnvironmentVariable> env_vars_;
    std::vector<bazel_re::Platform_Property> properties_;
    CacheFlag cache_flag_{CacheFlag::CacheOutput};
    std::chrono::milliseconds timeout_{kDefaultTimeout};

    BazelAction(std::shared_ptr<BazelNetwork> network,
                bazel_re::Digest root_digest,
                std::vector<std::string> command,
                std::vector<std::string> output_files,
                std::vector<std::string> output_dirs,
                std::map<std::string, std::string> const& env_vars,
                std::map<std::string, std::string> const& properties) noexcept;

    [[nodiscard]] auto CreateBundlesForAction(BlobContainer* blobs,
                                              bazel_re::Digest const& exec_dir,
                                              bool do_not_cache) const noexcept
        -> bazel_re::Digest;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_ACTION_HPP
