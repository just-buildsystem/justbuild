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
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/remote/bazel/bytestream_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/logger.hpp"

/// Implements client side for serivce defined here:
/// https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L317
class BazelCasClient {
  public:
    BazelCasClient(std::string const& server, Port port) noexcept;

    /// \brief Find missing blobs
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digests       The blob digests to search for
    /// \returns The digests of blobs not found in CAS
    [[nodiscard]] auto FindMissingBlobs(
        std::string const& instance_name,
        std::vector<bazel_re::Digest> const& digests) noexcept
        -> std::vector<bazel_re::Digest>;

    /// \brief Find missing blobs
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digests       The blob digests to search for
    /// \returns The digests of blobs not found in CAS
    [[nodiscard]] auto FindMissingBlobs(
        std::string const& instance_name,
        BlobContainer const& blob_container) noexcept
        -> std::vector<bazel_re::Digest>;

    /// \brief Upload multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] begin         Start of the blobs to upload
    /// \param[in] end           End of the blobs to upload
    /// \returns The digests of blobs successfully updated
    [[nodiscard]] auto BatchUpdateBlobs(
        std::string const& instance_name,
        std::vector<gsl::not_null<BazelBlob const*>>::const_iterator const&
            begin,
        std::vector<gsl::not_null<BazelBlob const*>>::const_iterator const&
            end) noexcept -> std::size_t;

    /// \brief Read multiple blobs in batch transfer
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] begin         Start of the blob digests to read
    /// \param[in] end           End of the blob digests to read
    /// \returns The blobs sucessfully read
    [[nodiscard]] auto BatchReadBlobs(
        std::string const& instance_name,
        std::vector<bazel_re::Digest>::const_iterator const& begin,
        std::vector<bazel_re::Digest>::const_iterator const& end) noexcept
        -> std::vector<BazelBlob>;

    [[nodiscard]] auto GetTree(std::string const& instance_name,
                               bazel_re::Digest const& root_digest,
                               std::int32_t page_size,
                               std::string const& page_token = "") noexcept
        -> std::vector<bazel_re::Directory>;

    /// \brief Upload single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] blob          The blob to upload
    /// \returns Boolean indicating successful upload
    [[nodiscard]] auto UpdateSingleBlob(std::string const& instance_name,
                                        BazelBlob const& blob) noexcept -> bool;

    /// \brief Read single blob via incremental bytestream reader
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns Incremental bytestream reader.
    [[nodiscard]] auto IncrementalReadSingleBlob(
        std::string const& instance_name,
        bazel_re::Digest const& digest) noexcept
        -> ByteStreamClient::IncrementalReader;

    /// \brief Read single blob via bytestream
    /// \param[in] instance_name Name of the CAS instance
    /// \param[in] digest        Blob digest to read
    /// \returns The blob successfully read
    [[nodiscard]] auto ReadSingleBlob(std::string const& instance_name,
                                      bazel_re::Digest const& digest) noexcept
        -> std::optional<BazelBlob>;

    /// @brief Split single blob into chunks
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Blob digest to be splitted
    /// @return The chunk digests of the splitted blob
    [[nodiscard]] auto SplitBlob(std::string const& instance_name,
                                 bazel_re::Digest const& blob_digest)
        const noexcept -> std::optional<std::vector<bazel_re::Digest>>;

    /// @brief Splice blob from chunks at the remote side
    /// @param[in] instance_name    Name of the CAS instance
    /// @param[in] blob_digest      Expected digest of the spliced blob
    /// @param[in] chunk_digests    The chunk digests of the splitted blob
    /// @return Whether the splice call was successful
    [[nodiscard]] auto SpliceBlob(
        std::string const& instance_name,
        bazel_re::Digest const& blob_digest,
        std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
        -> std::optional<bazel_re::Digest>;

    [[nodiscard]] auto BlobSplitSupport(
        std::string const& instance_name) const noexcept -> bool;

    [[nodiscard]] auto BlobSpliceSupport(
        std::string const& instance_name) const noexcept -> bool;

  private:
    std::unique_ptr<ByteStreamClient> stream_{};
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> stub_;
    Logger logger_{"RemoteCasClient"};

    template <class T_OutputIter>
    [[nodiscard]] auto FindMissingBlobs(std::string const& instance_name,
                                        T_OutputIter const& start,
                                        T_OutputIter const& end) noexcept
        -> std::vector<bazel_re::Digest>;

    template <typename T_Request, typename T_ForwardIter>
    [[nodiscard]] auto CreateBatchRequestsMaxSize(
        std::string const& instance_name,
        T_ForwardIter const& first,
        T_ForwardIter const& last,
        std::string const& heading,
        std::function<void(T_Request*,
                           typename T_ForwardIter::value_type const&)> const&
            request_builder) const noexcept -> std::vector<T_Request>;

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
    template <typename T_Content>
    struct RetryProcessBatchResponse {
        bool ok{false};
        std::vector<T_Content> result{};
        bool exit_retry_loop{false};
        std::optional<std::string> error_msg{};
    };

    // If this function is defined in the .cpp file, clang raises an error
    // while linking
    template <class T_Content, class T_Inner, class T_Response>
    [[nodiscard]] auto ProcessBatchResponse(
        T_Response const& response,
        std::function<void(std::vector<T_Content>*, T_Inner const&)> const&
            inserter) const noexcept -> RetryProcessBatchResponse<T_Content> {
        std::vector<T_Content> output;
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

    template <class T_Content, class T_Response>
    auto ProcessResponseContents(T_Response const& response) const noexcept
        -> std::vector<T_Content>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_CAS_CLIENT_HPP
