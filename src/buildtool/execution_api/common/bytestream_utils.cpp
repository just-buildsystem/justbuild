// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/common/bytestream_utils.hpp"

#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"

namespace {
/// \brief Split a string into parts with '/' delimiter
/// \param request String to be split
/// \return A vector of parts on success or an empty vector on failure.
[[nodiscard]] auto SplitRequest(std::string const& request) noexcept
    -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    try {
        std::size_t shift = 0;
        for (std::size_t length = 0; shift + length < request.size();
             ++length) {
            if (request[shift + length] == '/') {
                parts.emplace_back(&request[shift], length);
                shift += length + 1;
                length = 0;
            }
        }

        if (shift < request.size()) {
            parts.emplace_back(&request[shift], request.size() - shift);
        }
    } catch (...) {
        return {};
    }
    return parts;
}

[[nodiscard]] inline auto ToBazelDigest(std::string hash,
                                        std::size_t size) noexcept
    -> bazel_re::Digest {
    bazel_re::Digest digest{};
    digest.set_hash(std::move(hash));
    digest.set_size_bytes(static_cast<std::int64_t>(size));
    return digest;
}
}  // namespace

const std::set<std::string> ByteStreamUtils::kOtherReservedFragments =
    std::set<std::string>{"actions",
                          "actionResults",
                          "operations",
                          "capabilities",
                          "compressed-blobs"};

auto ByteStreamUtils::ReadRequest::ToString(
    std::string instance_name,
    ArtifactDigest const& digest) noexcept -> std::string {
    if (instance_name.empty()) {
        return fmt::format("{}/{}/{}",
                           ByteStreamUtils::kBlobs,
                           ArtifactDigestFactory::ToBazel(digest).hash(),
                           digest.size());
    }
    return fmt::format("{}/{}/{}/{}",
                       std::move(instance_name),
                       ByteStreamUtils::kBlobs,
                       ArtifactDigestFactory::ToBazel(digest).hash(),
                       digest.size());
}

auto ByteStreamUtils::ReadRequest::FromString(
    std::string const& request) noexcept -> std::optional<ReadRequest> {
    static constexpr std::size_t kHashIndexOffset = 1U;
    static constexpr std::size_t kSizeIndexOffset = 2U;
    static constexpr std::size_t kReadRequestPartsCountOffset = 3U;

    auto const parts = ::SplitRequest(request);
    int blobs_index = -1;
    for (int i = 0; i < parts.size(); i++) {
        if (parts[i].compare(ByteStreamUtils::kBlobs) == 0) {
            blobs_index = i;
            break;
        }
        if (parts[i].compare(ByteStreamUtils::kUploads) == 0) {
            // "uploads" not allowed in instance name
            return std::nullopt;
        }
        if (ByteStreamUtils::kOtherReservedFragments.contains(
                std::string{parts[i]})) {
            return std::nullopt;
        }
    }
    if (blobs_index < 0) {
        return std::nullopt;
    }

    if (parts.size() != blobs_index + kReadRequestPartsCountOffset) {
        return std::nullopt;
    }
    std::ostringstream instance_name{};
    for (int i = 0; i < blobs_index; i++) {
        instance_name << parts[i];
        if (i + 1 < blobs_index) {
            instance_name << "/";
        }
    }

    ReadRequest result;
    result.instance_name_ = instance_name.str();
    result.hash_ = std::string(parts[blobs_index + kHashIndexOffset]);
    try {
        result.size_ =
            std::stoul(std::string(parts[blobs_index + kSizeIndexOffset]));
    } catch (...) {
        return std::nullopt;
    }
    return result;
}

auto ByteStreamUtils::ReadRequest::GetDigest(HashFunction::Type hash_type)
    const noexcept -> expected<ArtifactDigest, std::string> {
    auto bazel_digest = ToBazelDigest(hash_, size_);
    return ArtifactDigestFactory::FromBazel(hash_type, bazel_digest);
}

auto ByteStreamUtils::WriteRequest::ToString(
    std::string instance_name,
    std::string uuid,
    ArtifactDigest const& digest) noexcept -> std::string {
    if (instance_name.empty()) {
        return fmt::format("{}/{}/{}/{}/{}",
                           ByteStreamUtils::kUploads,
                           std::move(uuid),
                           ByteStreamUtils::kBlobs,
                           ArtifactDigestFactory::ToBazel(digest).hash(),
                           digest.size());
    }
    return fmt::format("{}/{}/{}/{}/{}/{}",
                       std::move(instance_name),
                       ByteStreamUtils::kUploads,
                       std::move(uuid),
                       ByteStreamUtils::kBlobs,
                       ArtifactDigestFactory::ToBazel(digest).hash(),
                       digest.size());
}

auto ByteStreamUtils::WriteRequest::FromString(
    std::string const& request) noexcept -> std::optional<WriteRequest> {
    static constexpr std::size_t kUUIDIndexOffset = 1U;
    static constexpr std::size_t kBlobsIndexOffset = 2U;
    static constexpr std::size_t kHashIndexOffset = 3U;
    static constexpr std::size_t kSizeIndexOffset = 4U;
    static constexpr std::size_t kWriteRequestPartsCountOffset = 5U;

    auto const parts = ::SplitRequest(request);

    int uploads_index = -1;
    for (int i = 0; i < parts.size(); i++) {
        if (parts[i].compare(ByteStreamUtils::kUploads) == 0) {
            uploads_index = i;
            break;
        }
        if (parts[i].compare(ByteStreamUtils::kBlobs) == 0) {
            // "blobs" not allowed in instance name
            return std::nullopt;
        }
        if (ByteStreamUtils::kOtherReservedFragments.contains(
                std::string{parts[i]})) {
            return std::nullopt;
        }
    }
    if (uploads_index < 0) {
        return std::nullopt;
    }

    if ((parts.size() != uploads_index + kWriteRequestPartsCountOffset) or
        parts[uploads_index + kBlobsIndexOffset].compare(
            ByteStreamUtils::kBlobs) != 0) {
        return std::nullopt;
    }

    WriteRequest result;
    std::ostringstream instance_name{};
    for (int i = 0; i < uploads_index; i++) {
        instance_name << parts[i];
        if (i + 1 < uploads_index) {
            instance_name << "/";
        }
    }
    result.instance_name_ = instance_name.str();
    result.uuid_ = std::string(parts[uploads_index + kUUIDIndexOffset]);
    result.hash_ = std::string(parts[uploads_index + kHashIndexOffset]);
    try {
        result.size_ =
            std::stoul(std::string(parts[uploads_index + kSizeIndexOffset]));
    } catch (...) {
        return std::nullopt;
    }
    return result;
}

auto ByteStreamUtils::WriteRequest::GetDigest(HashFunction::Type hash_type)
    const noexcept -> expected<ArtifactDigest, std::string> {
    auto bazel_digest = ToBazelDigest(hash_, size_);
    return ArtifactDigestFactory::FromBazel(hash_type, bazel_digest);
}
