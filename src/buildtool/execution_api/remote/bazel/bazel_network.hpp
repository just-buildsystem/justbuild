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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP

#include <memory>
#include <optional>
#include <unordered_map>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
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
                   gsl::not_null<BazelCasClient*> const& cas,
                   std::vector<bazel_re::Digest> ids)
            : instance_name_{std::move(instance_name)},
              cas_{cas},
              ids_{std::move(ids)},
              begin_{ids_.begin()},
              current_{begin_} {};
    };

    BazelNetwork(std::string instance_name,
                 std::string const& host,
                 Port port,
                 ExecutionConfiguration const& exec_config) noexcept;

    /// \brief Check if digest exists in CAS
    /// \param[in]  digest  The digest to look up
    /// \returns True if digest exists in CAS, false otherwise
    [[nodiscard]] auto IsAvailable(
        bazel_re::Digest const& digest) const noexcept -> bool;

    [[nodiscard]] auto IsAvailable(std::vector<bazel_re::Digest> const& digests)
        const noexcept -> std::vector<bazel_re::Digest>;

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

    /// \brief Traverses a tree recursively and retrieves object infos of all
    /// found blobs. Tree objects are not added to the result list, but
    /// converted to a path name.
    /// \param tree_digest          Digest of the tree.
    /// \param parent               Local parent path.
    /// \param request_remote_tree  Query full tree from remote CAS.
    /// \returns Pair of vectors, first containing filesystem paths, second
    /// containing object infos.
    [[nodiscard]] auto RecursivelyReadTreeLeafs(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent,
        bool request_remote_tree = false) const noexcept
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<Artifact::ObjectInfo>>>;

    /// \brief Reads the flat content of a tree and returns object infos of all
    /// its direct entries (trees and blobs).
    /// \param tree_digest      Digest of the tree.
    /// \param parent           Local parent path.
    /// \returns Pair of vectors, first containing filesystem paths, second
    /// containing object infos.
    [[nodiscard]] auto ReadDirectTreeEntries(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent) const noexcept
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
