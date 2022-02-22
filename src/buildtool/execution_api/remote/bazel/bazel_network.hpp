#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP

#include <memory>
#include <optional>
#include <unordered_map>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/local_tree_map.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"

/// \brief Contains all network clients and is responsible for all network IO.
class BazelNetwork {
  public:
    class BlobReader {
        friend class BazelNetwork;

      public:
        // Obtain the next batch of blobs that can be transferred in a single
        // request.
        [[nodiscard]] auto Next() noexcept -> std::vector<BazelBlob>;

      private:
        std::string instance_name_;
        gsl::not_null<BazelCasClient*> cas_;
        std::vector<bazel_re::Digest> const ids_;
        std::vector<bazel_re::Digest>::const_iterator begin_;
        std::vector<bazel_re::Digest>::const_iterator current_;

        BlobReader(std::string instance_name,
                   gsl::not_null<BazelCasClient*> cas,
                   std::vector<bazel_re::Digest> ids)
            : instance_name_{std::move(instance_name)},
              cas_{std::move(cas)},
              ids_{std::move(ids)},
              begin_{ids_.begin()},
              current_{begin_} {};
    };

    BazelNetwork(std::string instance_name,
                 std::string const& host,
                 Port port,
                 ExecutionConfiguration const& exec_config,
                 std::shared_ptr<LocalTreeMap> tree_map = nullptr) noexcept;

    /// \brief Check if digest exists in CAS
    /// \param[in]  digest  The digest to look up
    /// \returns True if digest exists in CAS, false otherwise
    [[nodiscard]] auto IsAvailable(
        bazel_re::Digest const& digest) const noexcept -> bool;

    /// \brief Uploads blobs to CAS
    /// \param blobs              The blobs to upload
    /// \param skip_find_missing  Skip finding missing blobs, just upload all
    /// \returns True if upload was successful, false otherwise
    [[nodiscard]] auto UploadBlobs(BlobContainer const& blobs,
                                   bool skip_find_missing = false) noexcept
        -> bool;

    [[nodiscard]] auto ExecuteBazelActionSync(
        bazel_re::Digest const& action) noexcept
        -> std::optional<BazelExecutionClient::ExecutionOutput>;

    [[nodiscard]] auto ReadBlobs(
        std::vector<bazel_re::Digest> ids) const noexcept -> BlobReader;

    [[nodiscard]] auto IncrementalReadSingleBlob(bazel_re::Digest const& id)
        const noexcept -> ByteStreamClient::IncrementalReader;

    [[nodiscard]] auto GetCachedActionResult(
        bazel_re::Digest const& action,
        std::vector<std::string> const& output_files) const noexcept
        -> std::optional<bazel_re::ActionResult>;

    [[nodiscard]] auto ReadTreeInfos(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent,
        bool request_remote_tree = false) const noexcept
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<Artifact::ObjectInfo>>>;

    [[nodiscard]] auto DumpToStream(
        Artifact::ObjectInfo const& info,
        gsl::not_null<FILE*> const& stream) const noexcept -> bool;

  private:
    using DirectoryMap =
        std::unordered_map<bazel_re::Digest, bazel_re::Directory>;

    // Max size for batch transfers
    static constexpr std::size_t kMaxBatchTransferSize = 3 * 1024 * 1024;
    static_assert(kMaxBatchTransferSize < GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH,
                  "Max batch transfer size too large.");

    std::string const instance_name_{};
    ExecutionConfiguration exec_config_{};
    std::unique_ptr<BazelCasClient> cas_{};
    std::unique_ptr<BazelAcClient> ac_{};
    std::unique_ptr<BazelExecutionClient> exec_{};
    std::shared_ptr<LocalTreeMap> tree_map_{};

    template <class T_Iter>
    [[nodiscard]] auto DoUploadBlobs(T_Iter const& first,
                                     T_Iter const& last) noexcept -> bool;

    [[nodiscard]] auto ReadObjectInfosRecursively(
        std::optional<DirectoryMap> const& dir_map,
        BazelMsgFactory::InfoStoreFunc const& store_info,
        std::filesystem::path const& parent,
        bazel_re::Digest const& digest) const noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP
