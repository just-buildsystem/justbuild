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
#include <filesystem>
#include <memory>
#include <unordered_set>
#include <utility>

#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/back_map.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/path.hpp"

BazelNetworkReader::BazelNetworkReader(
    std::string instance_name,
    gsl::not_null<BazelCasClient const*> const& cas,
    HashFunction hash_function) noexcept
    : instance_name_{std::move(instance_name)},
      cas_{*cas},
      hash_function_{hash_function} {}

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
                         MessageLimits::kMaxGrpcLength);
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
        if (auto const content = blob->ReadContent()) {
            return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                *content);
        }
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
    auto const content = read_blob->ReadContent();
    if (content == nullptr) {
        return std::nullopt;
    }

    auto check_symlinks = [this](std::vector<ArtifactDigest> const& ids) {
        auto const blobs = ReadOrdered(ids);
        if (blobs.size() != ids.size()) {
            Logger::Log(LogLevel::Debug,
                        "BazelNetworkReader::ReadGitTree: read wrong number of "
                        "symlinks.");
            return false;
        }
        return std::all_of(
            blobs.begin(), blobs.end(), [](ArtifactBlob const& blob) {
                auto const content = blob.ReadContent();
                return content != nullptr and PathIsNonUpwards(*content);
            });
    };

    return GitRepo::ReadTreeData(*content,
                                 digest.hash(),
                                 check_symlinks,
                                 /*is_hex_id=*/true);
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
        auto const content = read_blob->ReadContent();
        return content != nullptr and std::invoke(dumper, *content);
    } catch (...) {
        return false;
    }
}

auto BazelNetworkReader::DumpBlob(Artifact::ObjectInfo const& info,
                                  DumpCallback const& dumper) const noexcept
    -> bool {
    auto reader = cas_.IncrementalReadSingleBlob(instance_name_, info.digest);
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

auto BazelNetworkReader::ReadSingleBlob(ArtifactDigest const& digest)
    const noexcept -> std::optional<ArtifactBlob> {
    return cas_.ReadSingleBlob(instance_name_, digest);
}

auto BazelNetworkReader::Read(std::unordered_set<ArtifactDigest> const& digests)
    const noexcept -> std::unordered_set<ArtifactBlob> {
    std::unordered_set<ArtifactBlob> read_result;
    read_result.reserve(digests.size());

    std::unordered_set<ArtifactDigest> to_batch;
    to_batch.reserve(digests.size());
    // Upload blobs that don't fit for batching: size is larger than limit or
    // unknown
    std::size_t const limit = cas_.GetMaxBatchTransferSize(instance_name_);
    for (auto const& digest : digests) {
        if (digest.size() == 0 or digest.size() > limit) {
            auto blob = cas_.ReadSingleBlob(instance_name_, digest);
            if (blob.has_value()) {
                read_result.emplace(*std::move(blob));
            }
        }
        else {
            to_batch.emplace(digest);
        }
    }

    // Batch remaining blobs:
    read_result.merge(cas_.BatchReadBlobs(instance_name_, to_batch));
    return read_result;
}

auto BazelNetworkReader::ReadOrdered(std::vector<ArtifactDigest> const& digests)
    const noexcept -> std::vector<ArtifactBlob> {
    auto const read_result =
        Read(std::unordered_set(digests.begin(), digests.end()));

    auto const back_map = BackMap<ArtifactDigest, ArtifactBlob>::Make(
        &read_result,
        [](ArtifactBlob const& blob) { return blob.GetDigest(); });
    if (back_map == nullptr) {
        return {};
    }

    std::vector<ArtifactBlob> artifacts;
    artifacts.reserve(digests.size());
    for (auto const& digest : digests) {
        if (auto value = back_map->GetReference(digest)) {
            artifacts.emplace_back(*value.value());
        }
    }
    return artifacts;
}

auto BazelNetworkReader::ReadIncrementally(
    gsl::not_null<std::vector<ArtifactDigest> const*> const& digests)
    const noexcept -> IncrementalReader {
    return IncrementalReader{*this, digests};
}

auto BazelNetworkReader::BatchReadBlobs(
    std::vector<ArtifactDigest> const& digests) const noexcept
    -> std::vector<ArtifactBlob> {
    // Batch blobs:
    auto const batched_blobs = cas_.BatchReadBlobs(
        instance_name_, std::unordered_set(digests.begin(), digests.end()));

    // Map digests to blobs for further lookup:
    auto const back_map = BackMap<ArtifactDigest, ArtifactBlob>::Make(
        &batched_blobs,
        [](ArtifactBlob const& blob) { return blob.GetDigest(); });

    if (back_map == nullptr) {
        return {};
    }

    // Restore the requested order:
    std::vector<ArtifactBlob> artifacts;
    artifacts.reserve(digests.size());
    for (ArtifactDigest const& digest : digests) {
        if (auto value = back_map->GetReference(digest)) {
            artifacts.emplace_back(*value.value());
        }
    }
    return artifacts;
}

auto BazelNetworkReader::GetMaxBatchTransferSize() const noexcept
    -> std::size_t {
    return cas_.GetMaxBatchTransferSize(instance_name_);
}

namespace {
[[nodiscard]] auto FindCurrentIterator(
    std::size_t content_limit,
    std::vector<ArtifactDigest>::const_iterator const& begin,
    std::vector<ArtifactDigest>::const_iterator const& end) noexcept {
    auto it =
        std::find_if(begin, end, [content_limit](ArtifactDigest const& digest) {
            auto const size = digest.size();
            return size == 0 or size > content_limit;
        });
    if (it == begin and begin != end) {
        ++it;
    }
    return it;
}
}  // namespace

BazelNetworkReader::IncrementalReader::Iterator::Iterator(
    BazelNetworkReader const& owner,
    std::vector<ArtifactDigest>::const_iterator begin,
    std::vector<ArtifactDigest>::const_iterator end) noexcept
    : owner_{owner}, begin_{begin}, end_{end} {
    current_ =
        FindCurrentIterator(owner_.GetMaxBatchTransferSize(), begin_, end_);
}

auto BazelNetworkReader::IncrementalReader::Iterator::operator*() const noexcept
    -> value_type {
    if (begin_ != current_) {
        if (std::distance(begin_, current_) > 1) {
            std::vector<ArtifactDigest> request{begin_, current_};
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
    current_ =
        FindCurrentIterator(owner_.GetMaxBatchTransferSize(), begin_, end_);
    return *this;
}
