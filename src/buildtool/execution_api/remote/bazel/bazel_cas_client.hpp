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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <grpcpp/support/status.h>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/remote/bazel/bytestream_client.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for serivce defined here:
/// https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L317
class BazelCasClient {
  public:
    explicit BazelCasClient(
        std::string const& server,
        Port port,
        gsl::not_null<Auth const*> const& auth,
        gsl::not_null<RetryConfig const*> const& retry_config) noexcept;

    /// \brief Find missing blobs
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digests       The blob digests to search for
    /// \returns The digests of blobs not found in CAS
    [[nodiscard]] auto FindMissingBlobs(
        std::string const& instance_name,
        std::unordered_set<bazel_re::Digest> const& digests) const noexcept
        -> std::unordered_set<bazel_re::Digest>;

    /// \brief Upload multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] begin         Start of the blobs to upload
    /// \param[in] end           End of the blobs to upload
    /// \returns The digests of blobs successfully updated
    [[nodiscard]] auto BatchUpdateBlobs(
        std::string const& instance_name,
        std::unordered_set<BazelBlob>::const_iterator const& begin,
        std::unordered_set<BazelBlob>::const_iterator const& end) const noexcept
        -> std::size_t;

    /// \brief Read multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] begin         Start of the blob digests to read
    /// \param[in] end           End of the blob digests to read
    /// \returns The blobs sucessfully read
    [[nodiscard]] auto BatchReadBlobs(
        std::string const& instance_name,
        std::vector<bazel_re::Digest>::const_iterator const& begin,
        std::vector<bazel_re::Digest>::const_iterator const& end) const noexcept
        -> std::vector<BazelBlob>;

    [[nodiscard]] auto GetTree(std::string const& instance_name,
                               bazel_re::Digest const& root_digest,
                               std::int32_t page_size,
                               std::string const& page_token = "")
        const noexcept -> std::vector<bazel_re::Directory>;

    /// \brief Upload single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] blob          The blob to upload
    /// \returns Boolean indicating successful upload
    [[nodiscard]] auto UpdateSingleBlob(std::string const& instance_name,
                                        BazelBlob const& blob) const noexcept
        -> bool;

    /// \brief Read single blob via incremental bytestream reader
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns Incremental bytestream reader.
    [[nodiscard]] auto IncrementalReadSingleBlob(
        std::string const& instance_name,
        bazel_re::Digest const& digest) const noexcept
        -> ByteStreamClient::IncrementalReader;

    /// \brief Read single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns The blob successfully read
    [[nodiscard]] auto ReadSingleBlob(std::string const& instance_name,
                                      bazel_re::Digest const& digest)
        const noexcept -> std::optional<BazelBlob>;

    /// @brief Split single blob into chunks
    /// @param[in] hash_function    Hash function to be used for creation of
    /// an empty blob.
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Blob digest to be splitted
    /// @return The chunk digests of the splitted blob
    [[nodiscard]] auto SplitBlob(HashFunction hash_function,
                                 std::string const& instance_name,
                                 bazel_re::Digest const& blob_digest)
        const noexcept -> std::optional<std::vector<bazel_re::Digest>>;

    /// @brief Splice blob from chunks at the remote side
    /// @param[in] hash_function    Hash function to be used for creation of
    /// an empty blob.
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Expected digest of the spliced blob
    /// @param[in] chunk_digests    The chunk digests of the splitted blob
    /// @return Whether the splice call was successful
    [[nodiscard]] auto SpliceBlob(
        HashFunction hash_function,
        std::string const& instance_name,
        bazel_re::Digest const& blob_digest,
        std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
        -> std::optional<bazel_re::Digest>;

    [[nodiscard]] auto BlobSplitSupport(
        HashFunction hash_function,
        std::string const& instance_name) const noexcept -> bool;

    [[nodiscard]] auto BlobSpliceSupport(
        HashFunction hash_function,
        std::string const& instance_name) const noexcept -> bool;

  private:
    std::unique_ptr<ByteStreamClient> stream_;
    RetryConfig const& retry_config_;
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> stub_;
    Logger logger_{"RemoteCasClient"};

    template <class TOutputIter>
    [[nodiscard]] auto FindMissingBlobs(std::string const& instance_name,
                                        TOutputIter const& start,
                                        TOutputIter const& end) const noexcept
        -> std::unordered_set<bazel_re::Digest>;

    template <typename TRequest, typename TForwardIter>
    [[nodiscard]] auto CreateBatchRequestsMaxSize(
        std::string const& instance_name,
        TForwardIter const& first,
        TForwardIter const& last,
        std::string const& heading,
        std::function<void(TRequest*,
                           typename TForwardIter::value_type const&)> const&
            request_builder) const noexcept -> std::vector<TRequest>;

    [[nodiscard]] static auto CreateUpdateBlobsSingleRequest(
        BazelBlob const& b) noexcept
        -> bazel_re::BatchUpdateBlobsRequest_Request;

    [[nodiscard]] static auto CreateGetTreeRequest(
        std::string const& instance_name,
        bazel_re::Digest const& root_digest,
        int page_size,
        std::string const& page_token) noexcept -> bazel_re::GetTreeRequest;

    /// \brief Utility class for supporting the Retry strategy while parsing a
    /// BatchResponse
    template <typename TContent>
    struct RetryProcessBatchResponse {
        bool ok{false};
        std::vector<TContent> result{};
        bool exit_retry_loop{false};
        std::optional<std::string> error_msg{};
    };

    // If this function is defined in the .cpp file, clang raises an error
    // while linking
    template <class TContent, class TInner, class TResponse>
    [[nodiscard]] auto ProcessBatchResponse(
        TResponse const& response,
        std::function<void(std::vector<TContent>*, TInner const&)> const&
            inserter) const noexcept -> RetryProcessBatchResponse<TContent> {
        std::vector<TContent> output;
        for (auto const& res : response.responses()) {
            auto const& res_status = res.status();
            if (res_status.code() == static_cast<int>(grpc::StatusCode::OK)) {
                inserter(&output, res);
            }
            else {
                auto exit_retry_loop =
                    (res_status.code() !=
                     static_cast<int>(grpc::StatusCode::UNAVAILABLE));
                return {.ok = false,
                        .exit_retry_loop = exit_retry_loop,
                        .error_msg =
                            fmt::format("While processing batch response: {}",
                                        res_status.ShortDebugString())};
            }
        }
        return {.ok = true, .result = std::move(output)};
    }

    template <class TContent, class TResponse>
    auto ProcessResponseContents(TResponse const& response) const noexcept
        -> std::vector<TContent>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP
