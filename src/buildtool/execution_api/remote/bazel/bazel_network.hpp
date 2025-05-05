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
#include <string>
#include <unordered_set>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/execution_config.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_capabilities_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

/// \brief Contains all network clients and is responsible for all network IO.
class BazelNetwork {
  public:
    explicit BazelNetwork(std::string instance_name,
                          std::string const& host,
                          Port port,
                          gsl::not_null<Auth const*> const& auth,
                          gsl::not_null<RetryConfig const*> const& retry_config,
                          ExecutionConfiguration const& exec_config,
                          HashFunction hash_function,
                          TmpDir::Ptr temp_space) noexcept;

    /// \brief Check if digest exists in CAS
    /// \param[in]  digest  The digest to look up
    /// \returns True if digest exists in CAS, false otherwise
    [[nodiscard]] auto IsAvailable(ArtifactDigest const& digest) const noexcept
        -> bool;

    [[nodiscard]] auto FindMissingBlobs(
        std::unordered_set<ArtifactDigest> const& digests) const noexcept
        -> std::unordered_set<ArtifactDigest>;

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
    [[nodiscard]] auto UploadBlobs(std::unordered_set<ArtifactBlob>&& blobs,
                                   bool skip_find_missing = false) noexcept
        -> bool;

    [[nodiscard]] auto ExecuteBazelActionSync(
        bazel_re::Digest const& action) noexcept
        -> std::optional<BazelExecutionClient::ExecutionOutput>;

    [[nodiscard]] auto CreateReader() const noexcept -> BazelNetworkReader;
    [[nodiscard]] auto GetHashFunction() const noexcept -> HashFunction {
        return hash_function_;
    }

    [[nodiscard]] auto GetTempSpace() const noexcept -> TmpDir::Ptr {
        return cas_->GetTempSpace();
    }

    [[nodiscard]] auto GetCachedActionResult(
        bazel_re::Digest const& action,
        std::vector<std::string> const& output_files) const noexcept
        -> std::optional<bazel_re::ActionResult>;

    [[nodiscard]] auto GetCapabilities() const noexcept -> Capabilities::Ptr {
        return capabilities_->GetCapabilities(instance_name_);
    }

  private:
    std::string const instance_name_;
    std::unique_ptr<BazelCapabilitiesClient> capabilities_;
    std::unique_ptr<BazelCasClient> cas_;
    std::unique_ptr<BazelAcClient> ac_;
    std::unique_ptr<BazelExecutionClient> exec_;
    ExecutionConfiguration exec_config_{};
    HashFunction hash_function_;

    [[nodiscard]] auto DoUploadBlobs(
        std::unordered_set<ArtifactBlob> blobs) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_NETWORK_HPP
