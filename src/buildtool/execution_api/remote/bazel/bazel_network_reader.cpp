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
        // TODO(denisov) Fix non-incremental read
        auto blobs = BatchReadBlobs(ids);
        if (blobs.size() > ids.size()) {
            Logger::Log(LogLevel::Debug, "received more blobs than requested.");
            return false;
        }
        return std::all_of(
            blobs.begin(), blobs.end(), [](ArtifactBlob const& blob) {
                return PathIsNonUpwards(*blob.data);
            });
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
    if (auto blob = cas_.ReadSingleBlob(instance_name_, digest)) {
        return ArtifactBlob{
            ArtifactDigest{blob->digest}, blob->data, blob->is_exec};
    }
    return std::nullopt;
}

auto BazelNetworkReader::BatchReadBlobs(
    std::vector<bazel_re::Digest> const& blobs) const noexcept
    -> std::vector<ArtifactBlob> {
    std::vector<BazelBlob> result =
        cas_.BatchReadBlobs(instance_name_, blobs.begin(), blobs.end());

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
