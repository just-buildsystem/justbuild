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
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/client_common.hpp"
#include "src/buildtool/common/remote/retry.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {

[[nodiscard]] auto GetContentSize(bazel_re::Digest const& digest) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(digest.size_bytes());
}

[[nodiscard]] auto GetContentSize(ArtifactBlob const& blob) noexcept
    -> std::size_t {
    return blob.GetContentSize();
}

template <typename TRequest,
          typename TIterator,
          typename TCreator,
          typename TValue = typename TIterator::value_type>
[[nodiscard]] auto InitRequest(TRequest* request,
                               TCreator const& request_creator,
                               TIterator begin,
                               TIterator end,
                               std::size_t message_limit,
                               std::optional<std::size_t> const& content_limit =
                                   std::nullopt) -> TIterator {
    std::size_t content_size = 0;
    for (auto it = begin; it != end; ++it) {
        std::optional<TRequest> to_merge = std::invoke(request_creator, *it);
        if (not to_merge.has_value()) {
            return it;
        }

        if (request->ByteSizeLong() + to_merge->ByteSizeLong() >
            message_limit) {
            return it;
        }
        if (content_limit.has_value() and
            content_size + GetContentSize(*it) > *content_limit) {
            return it;
        }
        request->MergeFrom(*std::move(to_merge));
        content_size += GetContentSize(*it);
    }
    return end;
}

// In order to determine whether blob splitting is supported at the remote, a
// trial request to the remote CAS service is issued. This is just a workaround
// until the blob split API extension is accepted as part of the official remote
// execution protocol. Then, the ordinary way to determine server capabilities
// can be employed by using the capabilities service.
[[nodiscard]] auto BlobSplitSupport(
    HashFunction hash_function,
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const&
        stub) noexcept -> bool {
    // Create empty blob.
    std::string empty_str{};
    auto const digest = BazelDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, empty_str);

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
    HashFunction hash_function,
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
    auto supported = ::BlobSplitSupport(hash_function, instance_name, stub);
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
    HashFunction hash_function,
    std::string const& instance_name,
    std::unique_ptr<bazel_re::ContentAddressableStorage::Stub> const&
        stub) noexcept -> bool {
    // Create empty blob.
    std::string empty_str{};
    auto const digest = BazelDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, empty_str);

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
    HashFunction hash_function,
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
    auto supported = ::BlobSpliceSupport(hash_function, instance_name, stub);
    logger->Emit(LogLevel::Debug,
                 "Blob splice support for \"{}\": {}",
                 instance_name,
                 supported);
    auto lock = std::unique_lock(mutex);
    blob_splice_support_map[instance_name] = supported;
    return supported;
}

}  // namespace

BazelCasClient::BazelCasClient(
    std::string const& server,
    Port port,
    gsl::not_null<Auth const*> const& auth,
    gsl::not_null<RetryConfig const*> const& retry_config,
    gsl::not_null<BazelCapabilitiesClient const*> const& capabilities,
    TmpDir::Ptr temp_space) noexcept
    : stream_{std::make_unique<ByteStreamClient>(server, port, auth)},
      retry_config_{*retry_config},
      capabilities_{*capabilities},
      temp_space_{std::move(temp_space)} {
    stub_ = bazel_re::ContentAddressableStorage::NewStub(
        CreateChannelWithCredentials(server, port, auth));
}

auto BazelCasClient::BatchReadBlobs(
    std::string const& instance_name,
    std::unordered_set<ArtifactDigest> const& blobs) const noexcept
    -> std::unordered_set<ArtifactBlob> {
    std::unordered_set<ArtifactBlob> result;
    if (blobs.empty()) {
        return result;
    }

    auto const max_content_size = GetMaxBatchTransferSize(instance_name);

    auto const back_map = BackMap<bazel_re::Digest, ArtifactDigest>::Make(
        &blobs, ArtifactDigestFactory::ToBazel);
    if (back_map == nullptr) {
        return result;
    }

    auto request_creator = [&instance_name](bazel_re::Digest const& digest) {
        bazel_re::BatchReadBlobsRequest request;
        request.set_instance_name(instance_name);
        *request.add_digests() = digest;
        return request;
    };

    try {
        result.reserve(blobs.size());
        bool has_failure = false;
        for (auto it_processed = back_map->GetKeys().begin(), it = it_processed;
             it_processed != back_map->GetKeys().end();
             it_processed = it) {
            bazel_re::BatchReadBlobsRequest request;
            it = InitRequest(&request,
                             request_creator,
                             it,
                             back_map->GetKeys().end(),
                             MessageLimits::kMaxGrpcLength,
                             max_content_size);
            // If no progress happens, fallback to streaming API:
            if (it == it_processed) {
                logger_.Emit(LogLevel::Warning,
                             "BatchReadBlobs: Failed to prepare request for "
                             "{}\nFalling back to streaming API.",
                             it->hash());
                std::optional<ArtifactBlob> blob;
                if (auto value = back_map->GetReference(*it)) {
                    blob = ReadSingleBlob(instance_name, *value.value());
                }

                if (blob.has_value()) {
                    result.emplace(*std::move(blob));
                }
                ++it;
                continue;
            }

            logger_.Emit(LogLevel::Trace,
                         "BatchReadBlobs - Request size: {} bytes\n",
                         request.ByteSizeLong());

            bool const retry_result = WithRetry(
                [this, &request, &result, &back_map]() -> RetryResponse {
                    bazel_re::BatchReadBlobsResponse response;
                    grpc::ClientContext context;
                    auto status =
                        stub_->BatchReadBlobs(&context, request, &response);
                    if (status.ok()) {
                        auto batch_response = ProcessBatchResponse<
                            ArtifactBlob,
                            bazel_re::BatchReadBlobsResponse_Response,
                            bazel_re::BatchReadBlobsResponse>(
                            response,
                            [this, &back_map](
                                std::vector<ArtifactBlob>* v,
                                bazel_re::BatchReadBlobsResponse_Response const&
                                    r) {
                                auto ref = back_map->GetReference(r.digest());
                                if (not ref.has_value()) {
                                    return;
                                }
                                auto blob = ArtifactBlob::FromTempFile(
                                    HashFunction{ref.value()->GetHashType()},
                                    ref.value()->IsTree() ? ObjectType::Tree
                                                          : ObjectType::File,
                                    temp_space_,
                                    r.data());
                                if (not blob.has_value()) {
                                    return;
                                }
                                v->emplace_back(*std::move(blob));
                            });
                        if (batch_response.ok) {
                            std::move(batch_response.result.begin(),
                                      batch_response.result.end(),
                                      std::inserter(result, result.end()));
                            return {.ok = true};
                        }
                        return {
                            .ok = false,
                            .exit_retry_loop = batch_response.exit_retry_loop,
                            .error_msg = batch_response.error_msg};
                    }
                    auto exit_retry_loop =
                        status.error_code() != grpc::StatusCode::UNAVAILABLE;
                    return {
                        .ok = false,
                        .exit_retry_loop = exit_retry_loop,
                        .error_msg = StatusString(status, "BatchReadBlobs")};
                },
                retry_config_,
                logger_);
            has_failure = has_failure or not retry_result;
        }
        if (has_failure) {
            logger_.Emit(LogLevel::Error, "Failed to BatchReadBlobs.");
        }
    } catch (...) {
        logger_.Emit(LogLevel::Error, "Caught exception in BatchReadBlobs");
    }
    return result;
}

auto BazelCasClient::GetTree(std::string const& instance_name,
                             bazel_re::Digest const& root_digest,
                             std::int32_t page_size,
                             std::string const& page_token) const noexcept
    -> std::vector<bazel_re::Directory> {
    auto request =
        CreateGetTreeRequest(instance_name, root_digest, page_size, page_token);

    grpc::ClientContext context;
    bazel_re::GetTreeResponse response;
    auto stream = stub_->GetTree(&context, request);

    std::vector<bazel_re::Directory> result;
    while (stream->Read(&response)) {
        std::move(response.mutable_directories()->begin(),
                  response.mutable_directories()->end(),
                  std::back_inserter(result));
        auto const& next_page_token = response.next_page_token();
        if (not next_page_token.empty()) {
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
                                      ArtifactBlob const& blob) const noexcept
    -> bool {
    logger_.Emit(LogLevel::Trace, [&blob]() {
        std::ostringstream oss{};
        oss << "upload single blob" << std::endl;
        oss << fmt::format(" - {}", blob.GetDigest().hash()) << std::endl;
        return oss.str();
    });

    if (not stream_->Write(instance_name, blob)) {
        logger_.Emit(LogLevel::Error,
                     "Failed to write {}:{}",
                     blob.GetDigest().hash(),
                     blob.GetDigest().size());
        return false;
    }
    return true;
}

auto BazelCasClient::IncrementalReadSingleBlob(std::string const& instance_name,
                                               ArtifactDigest const& digest)
    const noexcept -> ByteStreamClient::IncrementalReader {
    return stream_->IncrementalRead(instance_name, digest);
}

auto BazelCasClient::ReadSingleBlob(std::string const& instance_name,
                                    ArtifactDigest const& digest) const noexcept
    -> std::optional<ArtifactBlob> {
    return stream_->Read(instance_name, digest, temp_space_);
}

auto BazelCasClient::SplitBlob(HashFunction hash_function,
                               std::string const& instance_name,
                               bazel_re::Digest const& blob_digest)
    const noexcept -> std::optional<std::vector<bazel_re::Digest>> {
    if (not BlobSplitSupportCached(
            hash_function, instance_name, stub_, &logger_)) {
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
        retry_config_,
        logger_);
    if (not ok) {
        LogStatus(&logger_, LogLevel::Error, status, "SplitBlob");
        return std::nullopt;
    }
    std::vector<bazel_re::Digest> result;
    result.reserve(response.chunk_digests().size());
    std::move(response.mutable_chunk_digests()->begin(),
              response.mutable_chunk_digests()->end(),
              std::back_inserter(result));
    return result;
}

auto BazelCasClient::SpliceBlob(
    HashFunction hash_function,
    std::string const& instance_name,
    bazel_re::Digest const& blob_digest,
    std::vector<bazel_re::Digest> const& chunk_digests) const noexcept
    -> std::optional<bazel_re::Digest> {
    if (not BlobSpliceSupportCached(
            hash_function, instance_name, stub_, &logger_)) {
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
        retry_config_,
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
    HashFunction hash_function,
    std::string const& instance_name) const noexcept -> bool {
    return ::BlobSplitSupportCached(
        hash_function, instance_name, stub_, &logger_);
}

auto BazelCasClient::BlobSpliceSupport(
    HashFunction hash_function,
    std::string const& instance_name) const noexcept -> bool {
    return ::BlobSpliceSupportCached(
        hash_function, instance_name, stub_, &logger_);
}

auto BazelCasClient::FindMissingBlobs(
    std::string const& instance_name,
    std::unordered_set<ArtifactDigest> const& digests) const noexcept
    -> std::unordered_set<ArtifactDigest> {
    std::unordered_set<ArtifactDigest> result;
    if (digests.empty()) {
        return result;
    }

    auto const back_map = BackMap<bazel_re::Digest, ArtifactDigest>::Make(
        &digests, ArtifactDigestFactory::ToBazel);
    if (back_map == nullptr) {
        return digests;
    }

    auto request_creator = [&instance_name](bazel_re::Digest const& digest) {
        bazel_re::FindMissingBlobsRequest request;
        request.set_instance_name(instance_name);
        *request.add_blob_digests() = digest;
        return request;
    };

    try {
        result.reserve(digests.size());
        for (auto it_processed = back_map->GetKeys().begin(), it = it_processed;
             it_processed != back_map->GetKeys().end();
             it_processed = it) {
            bazel_re::FindMissingBlobsRequest request;
            it = InitRequest(&request,
                             request_creator,
                             it,
                             back_map->GetKeys().end(),
                             MessageLimits::kMaxGrpcLength);
            // If no progress happens, consider current digest missing
            if (it == it_processed) {
                logger_.Emit(
                    LogLevel::Warning,
                    "FindMissingBlobs: Failed to prepare request for {}",
                    it->hash());

                if (auto value = back_map->GetReference(*it)) {
                    result.emplace(*value.value());
                }
                ++it;
                continue;
            }
            logger_.Emit(LogLevel::Trace,
                         "FindMissingBlobs - Request size: {} bytes\n",
                         request.ByteSizeLong());

            bazel_re::FindMissingBlobsResponse response;
            auto [ok, status] = WithRetry(
                [this, &response, &request]() {
                    grpc::ClientContext context;
                    return stub_->FindMissingBlobs(
                        &context, request, &response);
                },
                retry_config_,
                logger_);
            if (ok) {
                for (auto const& batch :
                     *response.mutable_missing_blob_digests()) {
                    if (auto value = back_map->GetReference(batch)) {
                        result.emplace(*value.value());
                    }
                }
            }
            else {
                LogStatus(
                    &logger_, LogLevel::Error, status, "FindMissingBlobs");
                // Failed to get a response. Mark all requested digests missing:
                for (auto const& digest : request.blob_digests()) {
                    if (auto value = back_map->GetReference(digest)) {
                        result.emplace(*value.value());
                    }
                }
            }
        }
        logger_.Emit(LogLevel::Trace, [&digests, &result]() {
            std::ostringstream oss{};
            oss << "find missing blobs" << std::endl;
            for (auto const& digest : digests) {
                oss << fmt::format(" - {}", digest.hash()) << std::endl;
            }
            oss << "missing blobs" << std::endl;
            for (auto const& digest : result) {
                oss << fmt::format(" - {}", digest.hash()) << std::endl;
            }
            return oss.str();
        });
    } catch (...) {
        logger_.Emit(LogLevel::Error, "Caught exception in FindMissingBlobs");
    }
    return result;
}

auto BazelCasClient::BatchUpdateBlobs(std::string const& instance_name,
                                      std::unordered_set<ArtifactBlob> const&
                                          blobs) const noexcept -> std::size_t {
    if (blobs.empty()) {
        return 0;
    }

    auto const max_content_size = GetMaxBatchTransferSize(instance_name);

    auto request_creator = [&instance_name](ArtifactBlob const& blob)
        -> std::optional<bazel_re::BatchUpdateBlobsRequest> {
        auto const content = blob.ReadContent();
        if (content == nullptr) {
            return std::nullopt;
        }

        bazel_re::BatchUpdateBlobsRequest request;
        request.set_instance_name(instance_name);

        auto& r = *request.add_requests();
        (*r.mutable_digest()) =
            ArtifactDigestFactory::ToBazel(blob.GetDigest());
        r.set_data(*content);
        return request;
    };

    std::unordered_set<bazel_re::Digest> updated;
    try {
        updated.reserve(blobs.size());
        bool has_failure = false;
        for (auto it_processed = blobs.begin(), it = it_processed;
             it_processed != blobs.end();
             it_processed = it) {
            bazel_re::BatchUpdateBlobsRequest request;
            it = InitRequest(&request,
                             request_creator,
                             it,
                             blobs.end(),
                             MessageLimits::kMaxGrpcLength,
                             max_content_size);
            // If no progress happens, skip current blob
            if (it == it_processed) {
                logger_.Emit(
                    LogLevel::Warning,
                    "BatchUpdateBlobs: Failed to prepare request for {}",
                    it->GetDigest().hash());
                ++it;
                continue;
            }

            logger_.Emit(LogLevel::Trace,
                         "BatchUpdateBlobs - Request size: {} bytes\n",
                         request.ByteSizeLong());

            bool const retry_result = WithRetry(
                [this, &request, &updated]() -> RetryResponse {
                    bazel_re::BatchUpdateBlobsResponse response;
                    grpc::ClientContext context;
                    auto status =
                        stub_->BatchUpdateBlobs(&context, request, &response);
                    if (status.ok()) {
                        auto batch_response = ProcessBatchResponse<
                            bazel_re::Digest,
                            bazel_re::BatchUpdateBlobsResponse_Response>(
                            response,
                            [](std::vector<bazel_re::Digest>* v,
                               bazel_re::
                                   BatchUpdateBlobsResponse_Response const& r) {
                                v->push_back(r.digest());
                            });
                        if (batch_response.ok) {
                            std::move(batch_response.result.begin(),
                                      batch_response.result.end(),
                                      std::inserter(updated, updated.end()));
                            return {.ok = true};
                        }
                        return {
                            .ok = false,
                            .exit_retry_loop = batch_response.exit_retry_loop,
                            .error_msg = batch_response.error_msg};
                    }
                    return {
                        .ok = false,
                        .exit_retry_loop = status.error_code() !=
                                           grpc::StatusCode::UNAVAILABLE,
                        .error_msg = StatusString(status, "BatchUpdateBlobs")};
                },
                retry_config_,
                logger_,
                LogLevel::Performance);
            has_failure = has_failure or not retry_result;
        }
        if (has_failure) {
            logger_.Emit(LogLevel::Performance, "Failed to BatchUpdateBlobs.");
        }
    } catch (...) {
        logger_.Emit(LogLevel::Warning,
                     "Caught exception in DoBatchUpdateBlobs");
    }
    logger_.Emit(LogLevel::Trace, [&blobs, &updated]() {
        std::ostringstream oss{};
        oss << "upload blobs" << std::endl;
        for (auto const& blob : blobs) {
            oss << fmt::format(" - {}", blob.GetDigest().hash()) << std::endl;
        }
        oss << "received blobs" << std::endl;
        for (auto const& digest : updated) {
            oss << fmt::format(" - {}", digest.hash()) << std::endl;
        }
        return oss.str();
    });

    auto const missing = blobs.size() - updated.size();
    if (not updated.empty() and missing != 0) {
        // The remote execution protocol is a bit unclear about how to deal with
        // blob updates for which we got no response. While some clients
        // consider a blob update failed only if a failed response is received,
        // we are going extra defensive here and also consider missing responses
        // to be a failed blob update. Issue a retry for the missing blobs.
        logger_.Emit(LogLevel::Trace, "Retrying with missing blobs");
        std::unordered_set<ArtifactBlob> missing_blobs;
        missing_blobs.reserve(missing);
        for (auto const& blob : blobs) {
            auto bazel_digest =
                ArtifactDigestFactory::ToBazel(blob.GetDigest());
            if (not updated.contains(bazel_digest)) {
                missing_blobs.emplace(blob);
            }
        }
        return updated.size() + BatchUpdateBlobs(instance_name, missing_blobs);
    }
    if (updated.empty() and missing != 0) {
        // The batch upload did not make _any_ progress. So there is no value in
        // trying that again; instead, we fall back to uploading each blob
        // sequentially.
        logger_.Emit(LogLevel::Debug, "Falling back to sequential blob upload");
        return std::count_if(blobs.begin(),
                             blobs.end(),
                             [this, &instance_name](ArtifactBlob const& blob) {
                                 return UpdateSingleBlob(instance_name, blob);
                             });
    }
    return updated.size();
}

auto BazelCasClient::GetMaxBatchTransferSize(
    std::string const& instance_name) const noexcept -> std::size_t {
    return capabilities_.GetCapabilities(instance_name)->MaxBatchTransferSize;
}

auto BazelCasClient::CreateGetTreeRequest(
    std::string const& instance_name,
    bazel_re::Digest const& root_digest,
    int page_size,
    std::string const& page_token) noexcept -> bazel_re::GetTreeRequest {
    bazel_re::GetTreeRequest request;
    request.set_instance_name(instance_name);
    (*request.mutable_root_digest()) = root_digest;
    request.set_page_size(page_size);
    request.set_page_token(page_token);
    return request;
}
