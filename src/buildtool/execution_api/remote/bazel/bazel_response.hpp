#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP

#include <string>
#include <vector>

#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"

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

    auto ActionDigest() const noexcept -> std::string final {
        return action_id_;
    }

    auto Artifacts() const noexcept -> ArtifactInfos final;

  private:
    std::string action_id_{};
    std::shared_ptr<BazelNetwork> const network_{};
    std::shared_ptr<LocalTreeMap> const tree_map_{};
    BazelExecutionClient::ExecutionOutput output_{};

    BazelResponse(std::string action_id,
                  std::shared_ptr<BazelNetwork> network,
                  std::shared_ptr<LocalTreeMap> tree_map,
                  BazelExecutionClient::ExecutionOutput output)
        : action_id_{std::move(action_id)},
          network_{std::move(network)},
          tree_map_{std::move(tree_map)},
          output_{std::move(output)} {}

    [[nodiscard]] auto ReadStringBlob(bazel_re::Digest const& id) noexcept
        -> std::string;

    [[nodiscard]] static auto IsDigestNotEmpty(bazel_re::Digest const& id)
        -> bool {
        return id.size_bytes() != 0;
    }

    [[nodiscard]] auto UploadTreeMessageDirectories(
        bazel_re::Tree const& tree) const -> std::optional<ArtifactDigest>;

    [[nodiscard]] auto ProcessDirectoryMessage(bazel_re::Directory const& dir)
        const noexcept -> std::optional<BazelBlob>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_RESPONSE_HPP
