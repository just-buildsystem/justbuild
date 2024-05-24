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

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
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

    [[nodiscard]] auto SplitBlob(bazel_re::Digest const& blob_digest)
        const noexcept -> std::optional<std::vector<bazel_re::Digest>>;

    [[nodiscard]] auto SpliceBlob(
        bazel_re::Digest const& blob_digest,
        std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
        -> std::optional<bazel_re::Digest>;

    [[nodiscard]] auto BlobSplitSupport() const noexcept -> bool;

    [[nodiscard]] auto BlobSpliceSupport() const noexcept -> bool;

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

    /// \brief Query full tree from remote CAS. Note that this is currently not
    // supported by Buildbarn revision c3c06bbe2a.
    [[nodiscard]] auto QueryFullTree(bazel_re::Digest const& digest)
        const noexcept -> std::optional<std::vector<bazel_re::Directory>>;

  private:
    std::string const instance_name_{};
    ExecutionConfiguration exec_config_{};
    std::unique_ptr<BazelCasClient> cas_{};
    std::unique_ptr<BazelAcClient> ac_{};
    std::unique_ptr<BazelExecutionClient> exec_{};

    template <class T_Iter>
    [[nodiscard]] auto DoUploadBlobs(T_Iter const& first,
                                     T_Iter const& last) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP
