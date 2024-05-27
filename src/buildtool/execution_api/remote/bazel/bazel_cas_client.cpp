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

#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>

#include "grpcpp/grpcpp.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/retry.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/logging/log_level.hpp"

namespace {

[[nodiscard]] auto ToResourceName(std::string const& instance_name,
                                  bazel_re::Digest const& digest) noexcept
    -> std::string {
    return fmt::format(
        "{}/blobs/{}/{}", instance_name, digest.hash(), digest.size_bytes());
}

// In order to determine whether blob splitting is supported at the remote, a
// trial request to the remote CAS service is issued. This is just a workaround
// until the blob split API extension is accepted as part of the official remote
// execution protocol. Then, the ordinary way to determine server capabilities
// can be employed by using the capabilities service.
[[nodiscard]] auto BlobSplitSupport(
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const&
        stub) noexcept -> bool {
    // Create empty blob.
    std::string empty_str{};
    std::string hash = HashFunction::ComputeBlobHash(empty_str).HexString();
    bazel_re::Digest digest{};
    digest.set_hash(NativeSupport::Prefix(hash, false));
    digest.set_size_bytes(empty_str.size());

    // Upload empty blob.
    grpc::ClientContext update_context{};
    bazel_re::BatchUpdateBlobsRequest update_request{};
    bazel_re::BatchUpdateBlobsResponse update_response{};
    update_request.set_instance_name(instance_name);
    auto* request = update_request.add_requests();
    request->mutable_digest()->CopyFrom(digest);
    request->set_data(empty_str);
    grpc::Status update_status = stub->BatchUpdateBlobs(
        &update_context, update_request, &update_response);
    if (not update_status.ok()) {
        return false;
    }

    // Request splitting empty blob.
    grpc::ClientContext split_context{};
    bazel_re::SplitBlobRequest split_request{};
    bazel_re::SplitBlobResponse split_response{};
    split_request.set_instance_name(instance_name);
    split_request.mutable_blob_digest()->CopyFrom(digest);
    grpc::Status split_status =
        stub->SplitBlob(&split_context, split_request, &split_response);
    return split_status.ok();
}

// Cached version of blob-split support request.
[[nodiscard]] auto BlobSplitSupportCached(
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const& stub,
    Logger const* logger) noexcept -> bool {
    static auto mutex = std::shared_mutex{};
    static auto blob_split_support_map =
        std::unordered_map<std::string, bool>{};
    {
        auto lock = std::shared_lock(mutex);
        if (blob_split_support_map.contains(instance_name)) {
            return blob_split_support_map[instance_name];
        }
    }
    auto supported = ::BlobSplitSupport(instance_name, stub);
    logger->Emit(LogLevel::Debug,
                 "Blob split support for \"{}\": {}",
                 instance_name,
                 supported);
    auto lock = std::unique_lock(mutex);
    blob_split_support_map[instance_name] = supported;
    return supported;
}

// In order to determine whether blob splicing is supported at the remote, a
// trial request to the remote CAS service is issued. This is just a workaround
// until the blob splice API extension is accepted as part of the official
// remote execution protocol. Then, the ordinary way to determine server
// capabilities can be employed by using the capabilities service.
[[nodiscard]] auto BlobSpliceSupport(
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const&
        stub) noexcept -> bool {
    // Create empty blob.
    std::string empty_str{};
    std::string hash = HashFunction::ComputeBlobHash(empty_str).HexString();
    bazel_re::Digest digest{};
    digest.set_hash(NativeSupport::Prefix(hash, false));
    digest.set_size_bytes(empty_str.size());

    // Upload empty blob.
    grpc::ClientContext update_context{};
    bazel_re::BatchUpdateBlobsRequest update_request{};
    bazel_re::BatchUpdateBlobsResponse update_response{};
    update_request.set_instance_name(instance_name);
    auto* request = update_request.add_requests();
    request->mutable_digest()->CopyFrom(digest);
    request->set_data(empty_str);
    grpc::Status update_status = stub->BatchUpdateBlobs(
        &update_context, update_request, &update_response);
    if (not update_status.ok()) {
        return false;
    }

    // Request splicing empty blob.
    grpc::ClientContext splice_context{};
    bazel_re::SpliceBlobRequest splice_request{};
    bazel_re::SpliceBlobResponse splice_response{};
    splice_request.set_instance_name(instance_name);
    splice_request.mutable_blob_digest()->CopyFrom(digest);
    splice_request.add_chunk_digests()->CopyFrom(digest);
    grpc::Status splice_status =
        stub->SpliceBlob(&splice_context, splice_request, &splice_response);
    return splice_status.ok();
}

// Cached version of blob-splice support request.
[[nodiscard]] auto BlobSpliceSupportCached(
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const& stub,
    Logger const* logger) noexcept -> bool {
    static auto mutex = std::shared_mutex{};
    static auto blob_splice_support_map =
        std::unordered_map<std::string, bool>{};
    {
        auto lock = std::shared_lock(mutex);
        if (blob_splice_support_map.contains(instance_name)) {
            return blob_splice_support_map[instance_name];
        }
    }
    auto supported = ::BlobSpliceSupport(instance_name, stub);
    logger->Emit(LogLevel::Debug,
                 "Blob splice support for \"{}\": {}",
                 instance_name,
                 supported);
    auto lock = std::unique_lock(mutex);
    blob_splice_support_map[instance_name] = supported;
    return supported;
}

}  // namespace

BazelCasClient::BazelCasClient(std::string const& server, Port port) noexcept
    : stream_{std::make_unique<ByteStreamClient>(server, port)} {
    stub_ = bazel_re::ContentAddressableStorage::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto BazelCasClient::FindMissingBlobs(
    std::string const& instance_name,
    std::vector<bazel_re::Digest> const& digests) noexcept
    -> std::vector<bazel_re::Digest> {
    return FindMissingBlobs(instance_name, digests.begin(), digests.end());
}

auto BazelCasClient::FindMissingBlobs(
    std::string const& instance_name,
    BazelBlobContainer const& blob_container) noexcept
    -> std::vector<bazel_re::Digest> {
    auto digests_range = blob_container.Digests();
    return FindMissingBlobs(
        instance_name, digests_range.begin(), digests_range.end());
}

auto BazelCasClient::BatchReadBlobs(
    std::string const& instance_name,
    std::vector<bazel_re::Digest>::const_iterator const& begin,
    std::vector<bazel_re::Digest>::const_iterator const& end) noexcept
    -> std::vector<BazelBlob> {
    if (begin == end) {
        return {};
    }
    std::vector<BazelBlob> result{};
    try {
        auto requests =
            CreateBatchRequestsMaxSize<bazel_re::BatchReadBlobsRequest>(
                instance_name,
                begin,
                end,
                "BatchReadBlobs",
                [](bazel_re::BatchReadBlobsRequest* request,
                   bazel_re::Digest const& x) {
                    *(request->add_digests()) = x;
                });
        bazel_re::BatchReadBlobsResponse response;
        auto batch_read_blobs =
            [this, &response, &result](auto const& request) -> RetryResponse {
            grpc::ClientContext context;
            auto status = stub_->BatchReadBlobs(&context, request, &response);
            if (status.ok()) {
                auto batch_response = ProcessBatchResponse<
                    BazelBlob,
                    bazel_re::BatchReadBlobsResponse_Response,
                    bazel_re::BatchReadBlobsResponse>(
                    response,
                    [](std::vector<BazelBlob>* v,
                       bazel_re::BatchReadBlobsResponse_Response const& r) {
                        v->emplace_back(
                            r.digest(), r.data(), /*is_exec=*/false);
                    });
                if (batch_response.ok) {
                    result = std::move(batch_response.result);
                    return {.ok = true};
                }
                return {.ok = false,
                        .exit_retry_loop = batch_response.exit_retry_loop,
                        .error_msg = batch_response.error_msg};
            }
            auto exit_retry_loop =
                status.error_code() != grpc::StatusCode::UNAVAILABLE;
            return {.ok = false,
                    .exit_retry_loop = exit_retry_loop,
                    .error_msg = StatusString(status, "BatchReadBlobs")};
        };
        if (not std::all_of(std::begin(requests),
                            std::end(requests),
                            [this, &batch_read_blobs](auto const& request) {
                                return WithRetry(
                                    [&request, &batch_read_blobs]() {
                                        return batch_read_blobs(request);
                                    },
                                    logger_);
                            })) {
            logger_.Emit(LogLevel::Error, "Failed to BatchReadBlobs.");
        }
    } catch (...) {
        logger_.Emit(LogLevel::Error, "Caught exception in BatchReadBlobs");
    }
    return result;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto BazelCasClient::GetTree(std::string const& instance_name,
                             bazel_re::Digest const& root_digest,
                             std::int32_t page_size,
                             std::string const& page_token) noexcept
    -> std::vector<bazel_re::Directory> {
    auto request =
        CreateGetTreeRequest(instance_name, root_digest, page_size, page_token);

    grpc::ClientContext context;
    bazel_re::GetTreeResponse response;
    auto stream = stub_->GetTree(&context, request);

    std::vector<bazel_re::Directory> result;
    while (stream->Read(&response)) {
        result = ProcessResponseContents<bazel_re::Directory>(response);
        auto const& next_page_token = response.next_page_token();
        if (!next_page_token.empty()) {
            // recursively call this function with token for next page
            auto next_result =
                GetTree(instance_name, root_digest, page_size, next_page_token);
            std::move(next_result.begin(),
                      next_result.end(),
                      std::back_inserter(result));
        }
    }

    auto status = stream->Finish();
    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Error, status);
    }

    return result;
}

auto BazelCasClient::UpdateSingleBlob(std::string const& instance_name,
                                      BazelBlob const& blob) noexcept -> bool {
    logger_.Emit(LogLevel::Trace, [&blob]() {
        std::ostringstream oss{};
        oss << "upload single blob" << std::endl;
        oss << fmt::format(" - {}", blob.digest.hash()) << std::endl;
        return oss.str();
    });

    thread_local static std::string uuid{};
    if (uuid.empty()) {
        auto id = CreateProcessUniqueId();
        if (not id) {
            logger_.Emit(LogLevel::Debug, "Failed creating process unique id.");
            return false;
        }
        uuid = CreateUUIDVersion4(*id);
    }
    auto ok = stream_->Write(fmt::format("{}/uploads/{}/blobs/{}/{}",
                                         instance_name,
                                         uuid,
                                         blob.digest.hash(),
                                         blob.digest.size_bytes()),
                             *blob.data);
    if (!ok) {
        logger_.Emit(LogLevel::Error,
                     "Failed to write {}:{}",
                     blob.digest.hash(),
                     blob.digest.size_bytes());
    }
    return ok;
}

auto BazelCasClient::IncrementalReadSingleBlob(
    std::string const& instance_name,
    bazel_re::Digest const& digest) noexcept
    -> ByteStreamClient::IncrementalReader {
    return stream_->IncrementalRead(ToResourceName(instance_name, digest));
}

auto BazelCasClient::ReadSingleBlob(std::string const& instance_name,
                                    bazel_re::Digest const& digest) noexcept
    -> std::optional<BazelBlob> {
    if (auto data = stream_->Read(ToResourceName(instance_name, digest))) {
        // Recompute the digest from the received content to cross-check a
        // correct transmission.
        auto real_digest = static_cast<bazel_re::Digest>(
            NativeSupport::IsTree(digest.hash())
                ? ArtifactDigest::Create<ObjectType::Tree>(*data)
                : ArtifactDigest::Create<ObjectType::File>(*data));
        if (digest.hash() != real_digest.hash()) {
            logger_.Emit(LogLevel::Warning,
                         "Requested {}, but received {}",
                         digest.hash(),
                         real_digest.hash());
            return std::nullopt;
        }
        return BazelBlob{
            std::move(real_digest), std::move(*data), /*is_exec=*/false};
    }
    return std::nullopt;
}

auto BazelCasClient::SplitBlob(std::string const& instance_name,
                               bazel_re::Digest const& blob_digest)
    const noexcept -> std::optional<std::vector<bazel_re::Digest>> {
    if (not BlobSplitSupportCached(instance_name, stub_, &logger_)) {
        return std::nullopt;
    }
    bazel_re::SplitBlobRequest request{};
    request.set_instance_name(instance_name);
    request.mutable_blob_digest()->CopyFrom(blob_digest);
    request.set_chunking_algorithm(
        bazel_re::ChunkingAlgorithm_Value::ChunkingAlgorithm_Value_FASTCDC);
    bazel_re::SplitBlobResponse response{};
    auto [ok, status] = WithRetry(
        [this, &response, &request]() {
            grpc::ClientContext context;
            return stub_->SplitBlob(&context, request, &response);
        },
        logger_);
    if (not ok) {
        LogStatus(&logger_, LogLevel::Error, status, "SplitBlob");
        return std::nullopt;
    }
    return ProcessResponseContents<bazel_re::Digest>(response);
}

auto BazelCasClient::SpliceBlob(
    std::string const& instance_name,
    bazel_re::Digest const& blob_digest,
    std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
    -> std::optional<bazel_re::Digest> {
    if (not BlobSpliceSupportCached(instance_name, stub_, &logger_)) {
        return std::nullopt;
    }
    bazel_re::SpliceBlobRequest request{};
    request.set_instance_name(instance_name);
    request.mutable_blob_digest()->CopyFrom(blob_digest);
    std::copy(chunk_digests.cbegin(),
              chunk_digests.cend(),
              pb::back_inserter(request.mutable_chunk_digests()));
    bazel_re::SpliceBlobResponse response{};
    auto [ok, status] = WithRetry(
        [this, &response, &request]() {
            grpc::ClientContext context;
            return stub_->SpliceBlob(&context, request, &response);
        },
        logger_);
    if (not ok) {
        LogStatus(&logger_, LogLevel::Error, status, "SpliceBlob");
        return std::nullopt;
    }
    if (not response.has_blob_digest()) {
        return std::nullopt;
    }
    return response.blob_digest();
}

auto BazelCasClient::BlobSplitSupport(
    std::string const& instance_name) const noexcept -> bool {
    return ::BlobSplitSupportCached(instance_name, stub_, &logger_);
}

auto BazelCasClient::BlobSpliceSupport(
    std::string const& instance_name) const noexcept -> bool {
    return ::BlobSpliceSupportCached(instance_name, stub_, &logger_);
}

template <class T_ForwardIter>
auto BazelCasClient::FindMissingBlobs(std::string const& instance_name,
                                      T_ForwardIter const& start,
                                      T_ForwardIter const& end) noexcept
    -> std::vector<bazel_re::Digest> {
    std::vector<bazel_re::Digest> result;
    if (start == end) {
        return result;
    }
    try {
        result.reserve(std::distance(start, end));
        auto requests =
            CreateBatchRequestsMaxSize<bazel_re::FindMissingBlobsRequest>(
                instance_name,
                start,
                end,
                "FindMissingBlobs",
                [](bazel_re::FindMissingBlobsRequest* request,
                   bazel_re::Digest const& x) {
                    *(request->add_blob_digests()) = x;
                });
        for (auto const& request : requests) {
            bazel_re::FindMissingBlobsResponse response;
            auto [ok, status] = WithRetry(
                [this, &response, &request]() {
                    grpc::ClientContext context;
                    return stub_->FindMissingBlobs(
                        &context, request, &response);
                },
                logger_);
            if (ok) {
                auto batch =
                    ProcessResponseContents<bazel_re::Digest>(response);
                for (auto&& x : batch) {
                    result.emplace_back(std::move(x));
                }
            }
            else {
                LogStatus(
                    &logger_, LogLevel::Error, status, "FindMissingBlobs");
            }
        }
        logger_.Emit(LogLevel::Trace, [&start, &end, &result]() {
            std::ostringstream oss{};
            oss << "find missing blobs" << std::endl;
            std::for_each(start, end, [&oss](auto const& digest) {
                oss << fmt::format(" - {}", digest.hash()) << std::endl;
            });
            oss << "missing blobs" << std::endl;
            std::for_each(
                result.cbegin(), result.cend(), [&oss](auto const& digest) {
                    oss << fmt::format(" - {}", digest.hash()) << std::endl;
                });
            return oss.str();
        });
    } catch (...) {
        logger_.Emit(LogLevel::Error, "Caught exception in FindMissingBlobs");
    }
    return result;
}

auto BazelCasClient::BatchUpdateBlobs(
    std::string const& instance_name,
    std::vector<gsl::not_null<BazelBlob const*>>::const_iterator const& begin,
    std::vector<gsl::not_null<BazelBlob const*>>::const_iterator const&
        end) noexcept -> std::size_t {
    if (begin == end) {
        return 0;
    }
    std::vector<bazel_re::Digest> result;
    try {
        auto requests =
            CreateBatchRequestsMaxSize<bazel_re::BatchUpdateBlobsRequest>(
                instance_name,
                begin,
                end,
                "BatchUpdateBlobs",
                [](bazel_re::BatchUpdateBlobsRequest* request,
                   BazelBlob const* x) {
                    *(request->add_requests()) =
                        BazelCasClient::CreateUpdateBlobsSingleRequest(*x);
                });
        result.reserve(std::distance(begin, end));
        auto batch_update_blobs =
            [this, &result](auto const& request) -> RetryResponse {
            bazel_re::BatchUpdateBlobsResponse response;
            grpc::ClientContext context;
            auto status = stub_->BatchUpdateBlobs(&context, request, &response);
            if (status.ok()) {
                auto batch_response = ProcessBatchResponse<
                    bazel_re::Digest,
                    bazel_re::BatchUpdateBlobsResponse_Response>(
                    response,
                    [](std::vector<bazel_re::Digest>* v,
                       bazel_re::BatchUpdateBlobsResponse_Response const& r) {
                        v->push_back(r.digest());
                    });
                if (batch_response.ok) {
                    std::move(std::begin(batch_response.result),
                              std::end(batch_response.result),
                              std::back_inserter(result));
                    return {.ok = true};
                }
                return {.ok = false,
                        .exit_retry_loop = batch_response.exit_retry_loop,
                        .error_msg = batch_response.error_msg};
            }
            return {.ok = false,
                    .exit_retry_loop =
                        status.error_code() != grpc::StatusCode::UNAVAILABLE,
                    .error_msg = StatusString(status, "BatchUpdateBlobs")};
        };
        if (not std::all_of(std::begin(requests),
                            std::end(requests),
                            [this, &batch_update_blobs](auto const& request) {
                                return WithRetry(
                                    [&request, &batch_update_blobs]() {
                                        return batch_update_blobs(request);
                                    },
                                    logger_);
                            })) {
            logger_.Emit(LogLevel::Error, "Failed to BatchUpdateBlobs.");
        }
    } catch (...) {
        logger_.Emit(LogLevel::Error, "Caught exception in DoBatchUpdateBlobs");
    }
    logger_.Emit(LogLevel::Trace, [begin, end, &result]() {
        std::ostringstream oss{};
        oss << "upload blobs" << std::endl;
        std::for_each(begin, end, [&oss](BazelBlob const* blob) {
            oss << fmt::format(" - {}", blob->digest.hash()) << std::endl;
        });
        oss << "received blobs" << std::endl;
        std::for_each(
            result.cbegin(), result.cend(), [&oss](auto const& digest) {
                oss << fmt::format(" - {}", digest.hash()) << std::endl;
            });
        return oss.str();
    });

    return result.size();
}

namespace detail {

// Getter for response contents (needs specialization, never implemented)
template <class T_Content, class T_Response>
static auto GetResponseContents(T_Response const&) noexcept
    -> pb::RepeatedPtrField<T_Content> const&;

// Specialization of GetResponseContents for 'FindMissingBlobsResponse'
template <>
auto GetResponseContents<bazel_re::Digest, bazel_re::FindMissingBlobsResponse>(
    bazel_re::FindMissingBlobsResponse const& response) noexcept
    -> pb::RepeatedPtrField<bazel_re::Digest> const& {
    return response.missing_blob_digests();
}

// Specialization of GetResponseContents for 'GetTreeResponse'
template <>
auto GetResponseContents<bazel_re::Directory, bazel_re::GetTreeResponse>(
    bazel_re::GetTreeResponse const& response) noexcept
    -> pb::RepeatedPtrField<bazel_re::Directory> const& {
    return response.directories();
}

// Specialization of GetResponseContents for 'SplitBlobResponse'
template <>
auto GetResponseContents<bazel_re::Digest, bazel_re::SplitBlobResponse>(
    bazel_re::SplitBlobResponse const& response) noexcept
    -> pb::RepeatedPtrField<bazel_re::Digest> const& {
    return response.chunk_digests();
}

}  // namespace detail

template <typename T_Request, typename T_ForwardIter>
auto BazelCasClient::CreateBatchRequestsMaxSize(
    std::string const& instance_name,
    T_ForwardIter const& first,
    T_ForwardIter const& last,
    std::string const& heading,
    std::function<void(T_Request*,
                       typename T_ForwardIter::value_type const&)> const&
        request_builder) const noexcept -> std::vector<T_Request> {
    if (first == last) {
        return {};
    }
    std::vector<T_Request> result;
    T_Request accumulating_request;
    std::for_each(
        first,
        last,
        [&instance_name, &accumulating_request, &result, &request_builder](
            auto const& blob) {
            T_Request request;
            request.set_instance_name(instance_name);
            request_builder(&request, blob);
            if (accumulating_request.ByteSizeLong() + request.ByteSizeLong() >
                kMaxBatchTransferSize) {
                result.emplace_back(std::move(accumulating_request));
                accumulating_request = std::move(request);
            }
            else {
                accumulating_request.MergeFrom(request);
            }
        });
    result.emplace_back(std::move(accumulating_request));
    logger_.Emit(LogLevel::Trace, [&heading, &result]() {
        std::ostringstream oss{};
        std::size_t count{0};
        oss << heading << " - Request sizes:" << std::endl;
        std::for_each(
            result.begin(), result.end(), [&oss, &count](auto const& request) {
                oss << fmt::format(
                           " {}: {} bytes", ++count, request.ByteSizeLong())
                    << std::endl;
            });
        return oss.str();
    });
    return result;
}

auto BazelCasClient::CreateUpdateBlobsSingleRequest(BazelBlob const& b) noexcept
    -> bazel_re::BatchUpdateBlobsRequest_Request {
    bazel_re::BatchUpdateBlobsRequest_Request r{};
    r.set_allocated_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{b.digest}});
    r.set_data(*b.data);
    return r;
}

auto BazelCasClient::CreateGetTreeRequest(
    std::string const& instance_name,
    bazel_re::Digest const& root_digest,
    int page_size,
    std::string const& page_token) noexcept -> bazel_re::GetTreeRequest {
    bazel_re::GetTreeRequest request;
    request.set_instance_name(instance_name);
    request.set_allocated_root_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{root_digest}});
    request.set_page_size(page_size);
    request.set_page_token(page_token);
    return request;
}

template <class T_Content, class T_Response>
auto BazelCasClient::ProcessResponseContents(
    T_Response const& response) const noexcept -> std::vector<T_Content> {
    std::vector<T_Content> output;
    auto const& contents = detail::GetResponseContents<T_Content>(response);
    std::copy(contents.begin(), contents.end(), std::back_inserter(output));
    return output;
}
