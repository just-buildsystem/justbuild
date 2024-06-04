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

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/path.hpp"

BazelNetworkReader::BazelNetworkReader(std::string instance_name,
                                       BazelCasClient const& cas) noexcept
    : instance_name_{std::move(instance_name)}, cas_(cas) {}

BazelNetworkReader::BazelNetworkReader(
    BazelNetworkReader&& other,
    std::optional<ArtifactDigest> request_remote_tree) noexcept
    : instance_name_{other.instance_name_}, cas_(other.cas_) {
    if (Compatibility::IsCompatible() and request_remote_tree) {
        // Query full tree from remote CAS. Note that this is currently not
        // supported by Buildbarn revision c3c06bbe2a.
        auto full_tree = cas_.GetTree(
            instance_name_, *request_remote_tree, kMaxBatchTransferSize);
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
    auto read_blob = ReadSingleBlob(digest);
    if (not read_blob) {
        Logger::Log(LogLevel::Debug, "Tree {} not found in CAS", digest.hash());
        return std::nullopt;
    }
    auto check_symlinks = [this](std::vector<bazel_re::Digest> const& ids) {
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
                                 HashFunction::ComputeTreeHash(content).Bytes(),
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

auto BazelNetworkReader::MakeAuxiliaryMap(
    std::vector<bazel_re::Directory>&& full_tree) noexcept
    -> std::optional<DirectoryMap> {
    DirectoryMap result;
    result.reserve(full_tree.size());
    for (auto& dir : full_tree) {
        try {
            result.emplace(ArtifactDigest::Create<ObjectType::File>(
                               dir.SerializeAsString()),
                           std::move(dir));
        } catch (...) {
            return std::nullopt;
        }
    }
    return result;
}

auto BazelNetworkReader::ReadSingleBlob(ArtifactDigest const& digest)
    const noexcept -> std::optional<ArtifactBlob> {
    auto blob = cas_.ReadSingleBlob(instance_name_, digest);
    if (blob and BazelNetworkReader::Validate(*blob)) {
        return ArtifactBlob{
            ArtifactDigest{blob->digest}, blob->data, blob->is_exec};
    }
    return std::nullopt;
}

auto BazelNetworkReader::ReadIncrementally(
    std::vector<bazel_re::Digest> digests) const noexcept -> IncrementalReader {
    return IncrementalReader{*this, std::move(digests)};
}

auto BazelNetworkReader::BatchReadBlobs(
    std::vector<bazel_re::Digest> const& blobs) const noexcept
    -> std::vector<ArtifactBlob> {
    std::vector<BazelBlob> result =
        cas_.BatchReadBlobs(instance_name_, blobs.begin(), blobs.end());

    auto it =
        std::remove_if(result.begin(), result.end(), [](BazelBlob const& blob) {
            return not BazelNetworkReader::Validate(blob);
        });
    result.erase(it, result.end());

    std::vector<ArtifactBlob> artifacts;
    artifacts.reserve(result.size());
    std::transform(result.begin(),
                   result.end(),
                   std::back_inserter(artifacts),
                   [](BazelBlob const& blob) {
                       return ArtifactBlob{ArtifactDigest{blob.digest},
                                           blob.data,
                                           blob.is_exec};
                   });
    return artifacts;
}

auto BazelNetworkReader::Validate(BazelBlob const& blob) noexcept -> bool {
    ArtifactDigest const rehashed_digest =
        NativeSupport::IsTree(blob.digest.hash())
            ? ArtifactDigest::Create<ObjectType::Tree>(*blob.data)
            : ArtifactDigest::Create<ObjectType::File>(*blob.data);
    if (rehashed_digest == ArtifactDigest{blob.digest}) {
        return true;
    }
    Logger::Log(LogLevel::Warning,
                "Requested {}, but received {}",
                ArtifactDigest{blob.digest}.hash(),
                rehashed_digest.hash());
    return false;
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

BazelNetworkReader::IncrementalReader::iterator::iterator(
    BazelNetworkReader const& owner,
    std::vector<bazel_re::Digest>::const_iterator begin,
    std::vector<bazel_re::Digest>::const_iterator end) noexcept
    : owner_{owner}, begin_{begin}, end_{end} {
    current_ = FindCurrentIterator(begin_, end_);
}

auto BazelNetworkReader::IncrementalReader::iterator::operator*() const noexcept
    -> value_type {
    if (begin_ != current_) {
        if (std::distance(begin_, current_) > 1) {
            std::vector<bazel_re::Digest> request{begin_, current_};
            return owner_.BatchReadBlobs(request);
        }
        if (auto blob = owner_.ReadSingleBlob(ArtifactDigest{*begin_})) {
            return {std::move(*blob)};
        }
    }
    return {};
}

auto BazelNetworkReader::IncrementalReader::iterator::operator++() noexcept
    -> iterator& {
    begin_ = current_;
    current_ = FindCurrentIterator(begin_, end_);
    return *this;
}
