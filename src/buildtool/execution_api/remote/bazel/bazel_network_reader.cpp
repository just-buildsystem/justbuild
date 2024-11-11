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

#include "src/buildtool/execution_api/remote/bazel/bazel_network_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/path.hpp"

BazelNetworkReader::BazelNetworkReader(
    std::string instance_name,
    gsl::not_null<BazelCasClient const*> const& cas,
    gsl::not_null<HashFunction const*> const& hash_function) noexcept
    : instance_name_{std::move(instance_name)},
      cas_{*cas},
      hash_function_{*hash_function} {}

BazelNetworkReader::BazelNetworkReader(
    BazelNetworkReader&& other,
    std::optional<ArtifactDigest> request_remote_tree) noexcept
    : instance_name_{other.instance_name_},
      cas_{other.cas_},
      hash_function_{other.hash_function_} {
    if (not IsNativeProtocol() and request_remote_tree) {
        // Query full tree from remote CAS. Note that this is currently not
        // supported by Buildbarn revision c3c06bbe2a.
        auto full_tree =
            cas_.GetTree(instance_name_,
                         ArtifactDigestFactory::ToBazel(*request_remote_tree),
                         kMaxBatchTransferSize);
        auxiliary_map_ = MakeAuxiliaryMap(std::move(full_tree));
    }
}

auto BazelNetworkReader::ReadDirectory(ArtifactDigest const& digest)
    const noexcept -> std::optional<bazel_re::Directory> {
    if (auxiliary_map_) {
        auto it = auxiliary_map_->find(digest);
        if (it != auxiliary_map_->end()) {
            return it->second;
        }
    }

    if (auto blob = ReadSingleBlob(digest)) {
        return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
            *blob->data);
    }
    Logger::Log(
        LogLevel::Debug, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto BazelNetworkReader::ReadGitTree(ArtifactDigest const& digest)
    const noexcept -> std::optional<GitRepo::tree_entries_t> {
    ExpectsAudit(IsNativeProtocol());

    auto read_blob = ReadSingleBlob(digest);
    if (not read_blob) {
        Logger::Log(LogLevel::Debug, "Tree {} not found in CAS", digest.hash());
        return std::nullopt;
    }
    auto check_symlinks = [this](std::vector<ArtifactDigest> const& ids) {
        size_t const size = ids.size();
        size_t count = 0;
        for (auto blobs : ReadIncrementally(ids)) {
            if (count + blobs.size() > size) {
                Logger::Log(LogLevel::Debug,
                            "received more blobs than requested.");
                return false;
            }
            bool valid = std::all_of(
                blobs.begin(), blobs.end(), [](ArtifactBlob const& blob) {
                    return PathIsNonUpwards(*blob.data);
                });
            if (not valid) {
                return false;
            }
            count += blobs.size();
        }
        return true;
    };

    std::string const& content = *read_blob->data;
    return GitRepo::ReadTreeData(content,
                                 hash_function_.HashTreeData(content).Bytes(),
                                 check_symlinks,
                                 /*is_hex_id=*/false);
}

auto BazelNetworkReader::DumpRawTree(Artifact::ObjectInfo const& info,
                                     DumpCallback const& dumper) const noexcept
    -> bool {
    auto read_blob = ReadSingleBlob(info.digest);
    if (not read_blob) {
        Logger::Log(
            LogLevel::Debug, "Object {} not found in CAS", info.digest.hash());
        return false;
    }

    try {
        return std::invoke(dumper, *read_blob->data);
    } catch (...) {
        return false;
    }
}

auto BazelNetworkReader::DumpBlob(Artifact::ObjectInfo const& info,
                                  DumpCallback const& dumper) const noexcept
    -> bool {
    auto reader = cas_.IncrementalReadSingleBlob(
        instance_name_, ArtifactDigestFactory::ToBazel(info.digest));
    auto data = reader.Next();
    while (data and not data->empty()) {
        try {
            if (not std::invoke(dumper, *data)) {
                return false;
            }
        } catch (...) {
            return false;
        }
        data = reader.Next();
    }
    return data.has_value();
}

auto BazelNetworkReader::IsNativeProtocol() const noexcept -> bool {
    return ProtocolTraits::IsNative(hash_function_.GetType());
}

auto BazelNetworkReader::MakeAuxiliaryMap(
    std::vector<bazel_re::Directory>&& full_tree) const noexcept
    -> std::optional<DirectoryMap> {
    ExpectsAudit(not IsNativeProtocol());

    DirectoryMap result;
    result.reserve(full_tree.size());
    for (auto& dir : full_tree) {
        try {
            result.emplace(ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                               hash_function_, dir.SerializeAsString()),
                           std::move(dir));
        } catch (...) {
            return std::nullopt;
        }
    }
    return result;
}

auto BazelNetworkReader::ReadSingleBlob(bazel_re::Digest const& digest)
    const noexcept -> std::optional<ArtifactBlob> {
    auto blob = cas_.ReadSingleBlob(instance_name_, digest);
    if (not blob) {
        return std::nullopt;
    }
    auto hash_info = Validate(*blob);
    if (not hash_info) {
        return std::nullopt;
    }
    return ArtifactBlob{
        ArtifactDigest{*std::move(hash_info), blob->data->size()},
        blob->data,
        blob->is_exec};
}

auto BazelNetworkReader::ReadSingleBlob(ArtifactDigest const& digest)
    const noexcept -> std::optional<ArtifactBlob> {
    return ReadSingleBlob(ArtifactDigestFactory::ToBazel(digest));
}

auto BazelNetworkReader::ReadIncrementally(
    std::vector<ArtifactDigest> const& digests) const noexcept
    -> IncrementalReader {
    std::vector<bazel_re::Digest> bazel_digests;
    bazel_digests.reserve(digests.size());
    std::transform(digests.begin(),
                   digests.end(),
                   std::back_inserter(bazel_digests),
                   [](ArtifactDigest const& d) {
                       return ArtifactDigestFactory::ToBazel(d);
                   });
    return ReadIncrementally(std::move(bazel_digests));
}

auto BazelNetworkReader::ReadIncrementally(
    std::vector<bazel_re::Digest> digests) const noexcept -> IncrementalReader {
    return IncrementalReader{*this, std::move(digests)};
}

auto BazelNetworkReader::BatchReadBlobs(
    std::vector<bazel_re::Digest> const& blobs) const noexcept
    -> std::vector<ArtifactBlob> {
    std::vector<BazelBlob> const result =
        cas_.BatchReadBlobs(instance_name_, blobs.begin(), blobs.end());

    std::vector<ArtifactBlob> artifacts;
    artifacts.reserve(result.size());
    for (auto const& blob : result) {
        if (auto hash_info = Validate(blob)) {
            artifacts.emplace_back(
                ArtifactDigest{*std::move(hash_info), blob.data->size()},
                blob.data,
                blob.is_exec);
        }
    }
    return artifacts;
}

auto BazelNetworkReader::Validate(BazelBlob const& blob) const noexcept
    -> std::optional<HashInfo> {
    // validate digest
    auto requested_hash_info =
        BazelDigestFactory::ToHashInfo(hash_function_.GetType(), blob.digest);
    if (not requested_hash_info) {
        Logger::Log(LogLevel::Warning,
                    "BazelNetworkReader: {}",
                    std::move(requested_hash_info).error());
        return std::nullopt;
    }

    // rehash data
    auto rehashed_info = HashInfo::HashData(
        hash_function_, *blob.data, requested_hash_info->IsTree());

    // ensure rehashed data produce the same hash
    if (*requested_hash_info != rehashed_info) {
        Logger::Log(LogLevel::Warning,
                    "Requested {}, but received {}",
                    requested_hash_info->Hash(),
                    rehashed_info.Hash());
        return std::nullopt;
    }
    return rehashed_info;
}

namespace {
[[nodiscard]] auto FindBorderIterator(
    std::vector<bazel_re::Digest>::const_iterator const& begin,
    std::vector<bazel_re::Digest>::const_iterator const& end) noexcept {
    std::int64_t size = 0;
    for (auto it = begin; it != end; ++it) {
        std::int64_t const blob_size = it->size_bytes();
        size += blob_size;
        if (blob_size == 0 or size > kMaxBatchTransferSize) {
            return it;
        }
    }
    return end;
}

[[nodiscard]] auto FindCurrentIterator(
    std::vector<bazel_re::Digest>::const_iterator const& begin,
    std::vector<bazel_re::Digest>::const_iterator const& end) noexcept {
    auto it = FindBorderIterator(begin, end);
    if (it == begin and begin != end) {
        ++it;
    }
    return it;
}
}  // namespace

BazelNetworkReader::IncrementalReader::Iterator::Iterator(
    BazelNetworkReader const& owner,
    std::vector<bazel_re::Digest>::const_iterator begin,
    std::vector<bazel_re::Digest>::const_iterator end) noexcept
    : owner_{owner}, begin_{begin}, end_{end} {
    current_ = FindCurrentIterator(begin_, end_);
}

auto BazelNetworkReader::IncrementalReader::Iterator::operator*() const noexcept
    -> value_type {
    if (begin_ != current_) {
        if (std::distance(begin_, current_) > 1) {
            std::vector<bazel_re::Digest> request{begin_, current_};
            return owner_.BatchReadBlobs(request);
        }
        if (auto blob = owner_.ReadSingleBlob(*begin_)) {
            return {std::move(*blob)};
        }
    }
    return {};
}

auto BazelNetworkReader::IncrementalReader::Iterator::operator++() noexcept
    -> Iterator& {
    begin_ = current_;
    current_ = FindCurrentIterator(begin_, end_);
    return *this;
}
