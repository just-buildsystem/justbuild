#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/artifact.hpp"  // Artifact::ObjectInfo
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"

/// \brief Abstract remote execution API
/// Can be used to create actions.
class IExecutionApi {
  public:
    using Ptr = std::unique_ptr<IExecutionApi>;

    IExecutionApi() = default;
    IExecutionApi(IExecutionApi const&) = delete;
    IExecutionApi(IExecutionApi&&) = default;
    auto operator=(IExecutionApi const&) -> IExecutionApi& = delete;
    auto operator=(IExecutionApi &&) -> IExecutionApi& = default;
    virtual ~IExecutionApi() = default;

    /// \brief Create a new action.
    /// \param[in] root_digest  Digest of the build root.
    /// \param[in] command      Command as argv vector
    /// \param[in] output_files List of paths to output files.
    /// \param[in] output_dirs  List of paths to output directories.
    /// \param[in] env_vars     The environment variables to set.
    /// \param[in] properties   Platform properties to set.
    /// \returns The new action.
    [[nodiscard]] virtual auto CreateAction(
        ArtifactDigest const& root_digest,
        std::vector<std::string> const& command,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::map<std::string, std::string> const& env_vars,
        std::map<std::string, std::string> const& properties) noexcept
        -> IExecutionAction::Ptr = 0;

    /// \brief Retrieve artifacts from CAS and store to specified paths.
    /// Tree artifacts are resolved its containing file artifacts are
    /// recursively retrieved.
    [[nodiscard]] virtual auto RetrieveToPaths(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<std::filesystem::path> const& output_paths) noexcept
        -> bool = 0;

    /// \brief Retrieve artifacts from CAS and write to file descriptors.
    /// Tree artifacts are not resolved and instead the raw protobuf message
    /// will be written to fd.
    [[nodiscard]] virtual auto RetrieveToFds(
        std::vector<Artifact::ObjectInfo> const& artifacts_info,
        std::vector<int> const& fds) noexcept -> bool = 0;

    /// \brief Upload blobs to CAS. Uploads only the blobs that are not yet
    /// available in CAS, unless `skip_find_missing` is specified.
    /// \param blobs                Container of blobs to upload.
    /// \param skip_find_missing    Skip finding missing blobs, just upload all.
    /// NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] virtual auto Upload(BlobContainer const& blobs,
                                      bool skip_find_missing = false) noexcept
        -> bool = 0;

    [[nodiscard]] virtual auto UploadTree(
        std::vector<DependencyGraph::NamedArtifactNodePtr> const&
            artifacts) noexcept -> std::optional<ArtifactDigest> = 0;

    [[nodiscard]] virtual auto IsAvailable(
        ArtifactDigest const& digest) const noexcept -> bool = 0;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_APIHPP
