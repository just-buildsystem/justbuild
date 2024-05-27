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
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/path.hpp"

BazelNetworkReader::BazelNetworkReader(
    BazelNetwork const& network,
    std::optional<ArtifactDigest> request_remote_tree) noexcept
    : network_(network) {
    if (Compatibility::IsCompatible() and request_remote_tree) {
        auto full_tree = network_.QueryFullTree(*request_remote_tree);
        if (full_tree) {
            auxiliary_map_ = MakeAuxiliaryMap(std::move(*full_tree));
        }
    }
}

auto BazelNetworkReader::ReadDirectory(ArtifactDigest const& digest)
    const noexcept -> std::optional<bazel_re::Directory> {
    try {
        if (auxiliary_map_ and auxiliary_map_->contains(digest)) {
            return auxiliary_map_->at(digest);
        }
    } catch (...) {
        // fallthrough
    }

    auto blobs = network_.ReadBlobs({digest}).Next();
    if (blobs.size() == 1) {
        return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
            *blobs.at(0).data);
    }
    Logger::Log(
        LogLevel::Debug, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto BazelNetworkReader::ReadGitTree(ArtifactDigest const& digest)
    const noexcept -> std::optional<GitRepo::tree_entries_t> {
    auto blobs = network_.ReadBlobs({digest}).Next();
    if (blobs.size() == 1) {
        auto const& content = *blobs.at(0).data;
        auto check_symlinks = [this](std::vector<bazel_re::Digest> const& ids) {
            auto size = ids.size();
            auto reader = network_.ReadBlobs(ids);
            auto blobs = reader.Next();
            std::size_t count{};
            while (not blobs.empty()) {
                if (count + blobs.size() > size) {
                    Logger::Log(LogLevel::Debug,
                                "received more blobs than requested.");
                    return false;
                }
                for (auto const& blob : blobs) {
                    if (not PathIsNonUpwards(*blob.data)) {
                        return false;
                    }
                }
                count += blobs.size();
                blobs = reader.Next();
            }
            return true;
        };
        return GitRepo::ReadTreeData(
            content,
            HashFunction::ComputeTreeHash(content).Bytes(),
            check_symlinks,
            /*is_hex_id=*/false);
    }
    Logger::Log(LogLevel::Debug, "Tree {} not found in CAS", digest.hash());
    return std::nullopt;
}

auto BazelNetworkReader::DumpRawTree(Artifact::ObjectInfo const& info,
                                     DumpCallback const& dumper) const noexcept
    -> bool {
    auto blobs = network_.ReadBlobs({info.digest}).Next();
    if (blobs.size() != 1) {
        Logger::Log(
            LogLevel::Debug, "Object {} not found in CAS", info.digest.hash());
        return false;
    }

    try {
        return std::invoke(dumper, *blobs.at(0).data);
    } catch (...) {
        return false;
    }
}

auto BazelNetworkReader::DumpBlob(Artifact::ObjectInfo const& info,
                                  DumpCallback const& dumper) const noexcept
    -> bool {
    auto reader = network_.IncrementalReadSingleBlob(info.digest);
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
