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
            if (request.at(shift + length) == '/') {
                parts.emplace_back(&request.at(shift), length);
                shift += length + 1;
                length = 0;
            }
        }

        if (shift < request.size()) {
            parts.emplace_back(&request.at(shift), request.size() - shift);
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

ByteStreamUtils::ReadRequest::ReadRequest(
    std::string instance_name,
    bazel_re::Digest const& digest) noexcept
    : instance_name_{std::move(instance_name)},
      hash_{digest.hash()},
      size_{static_cast<std::size_t>(digest.size_bytes())} {}

ByteStreamUtils::ReadRequest::ReadRequest(std::string instance_name,
                                          ArtifactDigest const& digest) noexcept
    : instance_name_{std::move(instance_name)},
      hash_{ArtifactDigestFactory::ToBazel(digest).hash()},
      size_{digest.size()} {}

auto ByteStreamUtils::ReadRequest::ToString() && noexcept -> std::string {
    return fmt::format("{}/{}/{}/{}",
                       std::move(instance_name_),
                       ByteStreamUtils::kBlobs,
                       std::move(hash_),
                       size_);
}

auto ByteStreamUtils::ReadRequest::FromString(
    std::string const& request) noexcept -> std::optional<ReadRequest> {
    static constexpr std::size_t kInstanceNameIndex = 0U;
    static constexpr std::size_t kBlobsIndex = 1U;
    static constexpr std::size_t kHashIndex = 2U;
    static constexpr std::size_t kSizeIndex = 3U;
    static constexpr std::size_t kReadRequestPartsCount = 4U;

    auto const parts = ::SplitRequest(request);
    if (parts.size() != kReadRequestPartsCount or
        parts.at(kBlobsIndex).compare(ByteStreamUtils::kBlobs) != 0) {
        return std::nullopt;
    }

    ReadRequest result;
    result.instance_name_ = std::string(parts.at(kInstanceNameIndex));
    result.hash_ = std::string(parts.at(kHashIndex));
    try {
        result.size_ = std::stoi(std::string(parts.at(kSizeIndex)));
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

ByteStreamUtils::WriteRequest::WriteRequest(
    std::string instance_name,
    std::string uuid,
    bazel_re::Digest const& digest) noexcept
    : instance_name_{std::move(instance_name)},
      uuid_{std::move(uuid)},
      hash_{digest.hash()},
      size_{static_cast<std::size_t>(digest.size_bytes())} {}

ByteStreamUtils::WriteRequest::WriteRequest(
    std::string instance_name,
    std::string uuid,
    ArtifactDigest const& digest) noexcept
    : instance_name_{std::move(instance_name)},
      uuid_{std::move(uuid)},
      hash_{ArtifactDigestFactory::ToBazel(digest).hash()},
      size_{digest.size()} {}

auto ByteStreamUtils::WriteRequest::ToString() && noexcept -> std::string {
    return fmt::format("{}/{}/{}/{}/{}/{}",
                       std::move(instance_name_),
                       ByteStreamUtils::kUploads,
                       std::move(uuid_),
                       ByteStreamUtils::kBlobs,
                       std::move(hash_),
                       size_);
}

auto ByteStreamUtils::WriteRequest::FromString(
    std::string const& request) noexcept -> std::optional<WriteRequest> {
    static constexpr std::size_t kInstanceNameIndex = 0U;
    static constexpr std::size_t kUploadsIndex = 1U;
    static constexpr std::size_t kUUIDIndex = 2U;
    static constexpr std::size_t kBlobsIndex = 3U;
    static constexpr std::size_t kHashIndex = 4U;
    static constexpr std::size_t kSizeIndex = 5U;
    static constexpr std::size_t kWriteRequestPartsCount = 6U;

    auto const parts = ::SplitRequest(request);
    if (parts.size() != kWriteRequestPartsCount or
        parts.at(kUploadsIndex).compare(ByteStreamUtils::kUploads) != 0 or
        parts.at(kBlobsIndex).compare(ByteStreamUtils::kBlobs) != 0) {
        return std::nullopt;
    }

    WriteRequest result;
    result.instance_name_ = std::string(parts.at(kInstanceNameIndex));
    result.uuid_ = std::string(parts.at(kUUIDIndex));
    result.hash_ = std::string(parts.at(kHashIndex));
    try {
        result.size_ = std::stoul(std::string(parts.at(kSizeIndex)));
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
