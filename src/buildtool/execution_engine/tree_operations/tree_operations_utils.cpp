// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_engine/tree_operations/tree_operations_utils.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
struct TreeEntry {
    Artifact::ObjectInfo info{};
    std::optional<std::string> symlink_target{};
};

using TreeEntries = std::unordered_map<std::string, TreeEntry>;

[[nodiscard]] auto ParseBazelDirectory(std::string const& tree_data,
                                       HashFunction::Type hash_type) noexcept
    -> std::optional<TreeEntries> {
    bazel_re::Directory bazel_directory{};
    if (not bazel_directory.ParseFromString(tree_data)) {
        return std::nullopt;
    }

    // Collect all entries from bazel directory.
    TreeEntries tree_entries{};
    tree_entries.reserve(
        static_cast<size_t>(bazel_directory.files_size()) +
        static_cast<size_t>(bazel_directory.symlinks_size()) +
        static_cast<size_t>(bazel_directory.directories_size()));

    // Collect files.
    for (auto const& file : bazel_directory.files()) {
        auto digest =
            ArtifactDigestFactory::FromBazel(hash_type, file.digest());
        if (not digest) {
            return std::nullopt;
        }
        tree_entries.insert(
            {file.name(),
             TreeEntry{.info = Artifact::ObjectInfo{
                           .digest = *std::move(digest),
                           .type = file.is_executable() ? ObjectType::Executable
                                                        : ObjectType::File}}});
    }

    // Collect symlinks.
    HashFunction hash_function{hash_type};
    for (auto const& symlink : bazel_directory.symlinks()) {
        tree_entries.insert(
            {symlink.name(),
             TreeEntry{
                 .info =
                     Artifact::ObjectInfo{
                         .digest = ArtifactDigestFactory::HashDataAs<
                             ObjectType::File>(hash_function, symlink.target()),
                         .type = ObjectType::Symlink},
                 .symlink_target = symlink.target()}});
    }

    // Collect directories.
    for (auto const& dir : bazel_directory.directories()) {
        auto digest = ArtifactDigestFactory::FromBazel(hash_type, dir.digest());
        if (not digest) {
            return std::nullopt;
        }
        tree_entries.insert({dir.name(),
                             TreeEntry{.info = Artifact::ObjectInfo{
                                           .digest = *std::move(digest),
                                           .type = ObjectType::Tree}}});
    }
    return tree_entries;
}

[[nodiscard]] auto ParseGitTree(std::string const& tree_data,
                                ArtifactDigest const& tree_digest,
                                HashFunction::Type hash_type) noexcept
    -> std::optional<TreeEntries> {
    // For a tree overlay computation, the actual target of a symbolic
    // link is not relevant. Symbolic links are just considered as
    // regular blobs.
    auto git_entries = GitRepo::ReadTreeData(
        tree_data,
        tree_digest.hash(),
        [](std::vector<ArtifactDigest> const&) { return true; },
        /*is_hex_id=*/true);
    if (not git_entries) {
        return std::nullopt;
    }

    // Collect all entries from git tree.
    TreeEntries tree_entries{};
    tree_entries.reserve(git_entries->size());
    for (auto const& [git_hash, entries] : *git_entries) {
        // Pick the first entry for that git hash to calculate the
        // object info once, since all follow-up entries will be the
        // same object, just with a different name.
        auto const& first_entry = entries.front();
        auto digest =
            ArtifactDigestFactory::Create(hash_type,
                                          ToHexString(git_hash),
                                          /*size=*/0,
                                          IsTreeObject(first_entry.type));
        if (not digest) {
            return std::nullopt;
        }
        // Pick up all names for that git object and create a tree entry
        // for each of them.
        for (auto const& entry : entries) {
            tree_entries.insert(
                {entry.name,
                 TreeEntry{.info = Artifact::ObjectInfo{
                               .digest = *digest, .type = first_entry.type}}});
        }
    }
    return tree_entries;
}

[[nodiscard]] auto ReadTree(IExecutionApi const& api,
                            Artifact::ObjectInfo const& tree_info) noexcept
    -> expected<TreeEntries, std::string> {
    // Fetch tree data.
    auto tree_data = api.RetrieveToMemory(tree_info);
    if (not tree_data) {
        return unexpected{
            fmt::format("Failed to fetch tree: {}", tree_info.ToString())};
    }

    // Parse tree data.
    auto tree_entries =
        ProtocolTraits::IsNative(api.GetHashType())
            ? ParseGitTree(*tree_data, tree_info.digest, api.GetHashType())
            : ParseBazelDirectory(*tree_data, api.GetHashType());
    if (not tree_entries) {
        return unexpected{
            fmt::format("Failed to parse tree: {}", tree_info.ToString())};
    }
    return *tree_entries;
}

[[nodiscard]] auto SerializeBazelDirectory(
    TreeEntries const& tree_entries) noexcept -> std::optional<std::string> {
    // Convert tree entries to bazel directory.
    bazel_re::Directory bazel_directory{};
    for (auto const& [name, entry] : tree_entries) {
        switch (entry.info.type) {
            case ObjectType::File: {
                auto* file = bazel_directory.add_files();
                file->set_name(name);
                file->mutable_digest()->CopyFrom(
                    ArtifactDigestFactory::ToBazel(entry.info.digest));
                file->set_is_executable(entry.info.type ==
                                        ObjectType::Executable);
                break;
            }
            case ObjectType::Symlink: {
                auto* symlink = bazel_directory.add_symlinks();
                symlink->set_name(name);
                symlink->set_target(*entry.symlink_target);
                break;
            }
            case ObjectType::Tree: {
                auto* dir = bazel_directory.add_directories();
                dir->set_name(name);
                dir->mutable_digest()->CopyFrom(
                    ArtifactDigestFactory::ToBazel(entry.info.digest));
                break;
            }
            default: {
                return std::nullopt;
            }
        }
    }

    // Serialize bazel directory.
    return bazel_directory.SerializeAsString();
}

[[nodiscard]] auto SerializeGitTree(TreeEntries const& tree_entries) noexcept
    -> std::optional<std::string> {
    // Convert tree entries to git entries.
    GitRepo::tree_entries_t git_entries{};
    git_entries.reserve(tree_entries.size());
    for (auto const& [name, entry] : tree_entries) {
        auto git_hash = FromHexString(entry.info.digest.hash());
        if (not git_hash) {
            return std::nullopt;
        }
        auto it = git_entries.find(*git_hash);
        if (it == git_entries.end()) {
            git_entries.insert({*git_hash, std::vector<GitRepo::TreeEntry>{}});
        }
        git_entries.at(*git_hash).emplace_back(name, entry.info.type);
    }

    // Serialize git entries.
    auto git_tree = GitRepo::CreateShallowTree(git_entries);
    if (not git_tree) {
        return std::nullopt;
    }
    return git_tree->second;
}

[[nodiscard]] auto WriteTree(IExecutionApi const& api,
                             TreeEntries const& tree_entries) noexcept
    -> expected<ArtifactDigest, std::string> {
    // Serialize tree entries.
    auto tree_data = ProtocolTraits::IsNative(api.GetHashType())
                         ? SerializeGitTree(tree_entries)
                         : SerializeBazelDirectory(tree_entries);
    if (not tree_data) {
        return unexpected{fmt::format("Failed to serialize tree entries")};
    }

    // Write tree data.
    auto tree_blob = ArtifactBlob::FromMemory(HashFunction{api.GetHashType()},
                                              ObjectType::Tree,
                                              *std::move(tree_data));
    if (not tree_blob) {
        return unexpected{fmt::format("Failed to create tree blob")};
    }
    if (api.Upload(std::unordered_set{*tree_blob})) {
        return tree_blob->GetDigest();
    }
    return unexpected{fmt::format("Failed to upload tree blob")};
}

}  // namespace

auto TreeOperationsUtils::ComputeTreeOverlay(
    IExecutionApi const& api,
    Artifact::ObjectInfo const& base_tree_info,
    Artifact::ObjectInfo const& other_tree_info,
    bool disjoint,
    std::filesystem::path const& where) noexcept
    -> expected<Artifact::ObjectInfo, std::string> {
    // Ensure that both objects are actually trees.
    Ensures(IsTreeObject(base_tree_info.type) and
            IsTreeObject(other_tree_info.type));

    Logger::Log(LogLevel::Trace,
                "Tree overlay {} vs {}",
                base_tree_info.ToString(),
                other_tree_info.ToString());

    // Early return if both trees are the same.
    if (base_tree_info == other_tree_info) {
        return base_tree_info;
    }

    // Read base tree.
    auto base_tree = ReadTree(api, base_tree_info);
    if (not base_tree) {
        return unexpected{base_tree.error()};
    }

    // Read other tree.
    auto other_tree = ReadTree(api, other_tree_info);
    if (not other_tree) {
        return unexpected{other_tree.error()};
    }

    // Compute tree overlay.
    TreeEntries overlay_tree{*other_tree};  // Make a copy of other tree.
    for (auto const& [base_name, base_entry] : *base_tree) {
        auto it = overlay_tree.find(base_name);
        if (it == overlay_tree.end()) {
            // No naming conflict detected, add entry to the other tree.
            overlay_tree.insert({base_name, base_entry});
            continue;
        }

        if (it->second.info == base_entry.info) {
            // Naming conflict detected, but names point to the same
            // object, no conflict.
            continue;
        }

        // Naming conflict detected and names point to different
        // objects.
        if (IsTreeObject(base_entry.info.type) and
            IsTreeObject(it->second.info.type)) {
            // If both objects are trees, recursively compute tree overlay.
            auto tree_info = ComputeTreeOverlay(api,
                                                base_entry.info,
                                                it->second.info,
                                                disjoint,
                                                where / base_name);
            if (not tree_info) {
                return unexpected{tree_info.error()};
            }
            overlay_tree[base_name] = TreeEntry{.info = *std::move(tree_info)};
            continue;
        }

        // If both objects are not trees, actual conflict detected.
        if (disjoint) {
            return unexpected{
                fmt::format("Conflict at path {}:\ndifferent objects {} vs {}",
                            nlohmann::json((where / base_name).string()).dump(),
                            base_entry.info.ToString(),
                            it->second.info.ToString())};
        }

        // Ignore conflict, keep entry from other tree.
    }

    // Write tree overlay.
    auto digest = WriteTree(api, overlay_tree);
    auto overlay_tree_info = Artifact::ObjectInfo{.digest = *std::move(digest),
                                                  .type = ObjectType::Tree};
    Logger::Log(LogLevel::Trace,
                "Tree overlay result: {}",
                overlay_tree_info.ToString());
    return overlay_tree_info;
}
