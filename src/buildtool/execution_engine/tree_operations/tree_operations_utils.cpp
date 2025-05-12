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

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <type_traits>  // for remove_reference
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/hex_string.hpp"

auto TreeOperationsUtils::ParseBazelDirectory(
    std::string const& tree_data,
    HashFunction::Type hash_type) noexcept -> std::optional<TreeEntries> {
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

auto TreeOperationsUtils::ParseGitTree(std::string const& tree_data,
                                       ArtifactDigest const& tree_digest,
                                       HashFunction::Type hash_type) noexcept
    -> std::optional<TreeEntries> {
    // For a tree-overlay computation, the actual target of a symbolic
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

auto TreeOperationsUtils::ReadTree(
    IExecutionApi const& api,
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

auto TreeOperationsUtils::SerializeBazelDirectory(
    TreeEntries const& tree_entries) noexcept -> std::optional<std::string> {
    // Sort tree entry names, so we can process them in the correct order.
    auto sorted = std::set<std::string>{};
    std::transform(tree_entries.begin(),
                   tree_entries.end(),
                   std::inserter(sorted, sorted.end()),
                   [](auto const& name_entry) { return name_entry.first; });
    // Convert tree entries to bazel directory.
    bazel_re::Directory bazel_directory{};
    for (auto const& name : sorted) {
        auto const& entry = tree_entries.at(name);
        switch (entry.info.type) {
            case ObjectType::File:
            case ObjectType::Executable: {
                auto* file = bazel_directory.add_files();
                file->set_name(name);
                *file->mutable_digest() =
                    ArtifactDigestFactory::ToBazel(entry.info.digest);
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
                *dir->mutable_digest() =
                    ArtifactDigestFactory::ToBazel(entry.info.digest);
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

auto TreeOperationsUtils::SerializeGitTree(
    TreeEntries const& tree_entries) noexcept -> std::optional<std::string> {
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

auto TreeOperationsUtils::WriteTree(IExecutionApi const& api,
                                    TreeEntries const& tree_entries) noexcept
    -> expected<Artifact::ObjectInfo, std::string> {
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
        return Artifact::ObjectInfo{.digest = tree_blob->GetDigest(),
                                    .type = ObjectType::Tree};
    }
    return unexpected{fmt::format("Failed to upload tree blob")};
}

auto TreeOperationsUtils::CreateTreeOverlayMap(IExecutionApi const& api,
                                               bool disjoint) noexcept
    -> AsyncMapConsumer<TreePair, Artifact::ObjectInfo> {
    auto value_creator = [&api, disjoint](auto /*unused*/,
                                          auto const& setter,
                                          auto const& logger,
                                          auto const& subcaller,
                                          auto const& key) {
        auto const& base_tree_info = key.trees.first;
        auto const& other_tree_info = key.trees.second;

        Logger::Log(LogLevel::Trace,
                    "Compute tree overlay:\n  - {}\n  - {}",
                    base_tree_info.ToString(),
                    other_tree_info.ToString());

        // Wrap logger for this tree-overlay computation.
        auto new_logger = std::make_shared<AsyncMapConsumerLogger>(
            [logger, base_tree_info, other_tree_info](auto const& msg,
                                                      auto fatal) {
                (*logger)(fmt::format("While merging the trees:\n  - {}\n  "
                                      "- {}\n{}",
                                      base_tree_info.ToString(),
                                      other_tree_info.ToString(),
                                      msg),
                          fatal);
            });

        // Ensure that both objects are actually trees.
        if (not IsTreeObject(base_tree_info.type) or
            not IsTreeObject(other_tree_info.type)) {
            (*new_logger)(fmt::format("Both objects have to be trees."),
                          /*fatal=*/true);
            return;
        }

        // Early return if both trees are the same.
        if (base_tree_info == other_tree_info) {
            (*setter)(Artifact::ObjectInfo{base_tree_info});
            return;
        }

        // Read base tree.
        auto base_tree = ReadTree(api, base_tree_info);
        if (not base_tree) {
            (*new_logger)(base_tree.error(), /*fatal=*/true);
            return;
        }

        // Read other tree.
        auto other_tree = ReadTree(api, other_tree_info);
        if (not other_tree) {
            (*new_logger)(other_tree.error(), /*fatal=*/true);
            return;
        }

        // Compute tree overlay. If two trees conflict, collect them and
        // process them in the subcaller.
        TreeEntries overlay_tree{*other_tree};  // Make a copy of other tree.
        std::vector<TreePair> keys{};
        std::vector<std::string> base_names{};
        auto min_size = std::min(base_tree->size(), other_tree->size());
        keys.reserve(min_size);
        base_names.reserve(min_size);
        for (auto& [base_name, base_entry] : *base_tree) {
            auto it = overlay_tree.find(base_name);
            if (it == overlay_tree.end()) {
                // No naming conflict detected, add entry to the other
                // tree.
                overlay_tree[std::move(base_name)] = std::move(base_entry);
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
                // If both objects are trees, compute tree overlay in
                // the subcaller.
                keys.emplace_back(std::make_pair(std::move(base_entry.info),
                                                 std::move(it->second.info)));
                base_names.emplace_back(std::move(base_name));
                continue;
            }

            // If both objects are not trees, actual conflict detected.
            if (disjoint) {
                (*new_logger)(fmt::format("Naming conflict detected at path "
                                          "{}:\n  - {}\n  - {}",
                                          nlohmann::json(base_name).dump(),
                                          base_entry.info.ToString(),
                                          it->second.info.ToString()),
                              /*fatal=*/true);
                return;
            }

            // Ignore conflict, keep entry from other tree.
        }

        (*subcaller)(
            keys,
            [setter,
             new_logger,
             &api,
             base_names = std::move(base_names),
             partial_overlay_tree =
                 std::move(overlay_tree)](auto const& values) {
                // Insert computed tree overlays into tree-overlay
                // entries.
                TreeEntries overlay_tree{partial_overlay_tree};
                for (size_t i = 0; i < values.size(); ++i) {
                    // Create copy of the value.
                    overlay_tree[base_names[i]] =
                        TreeEntry{.info = *(values[i])};
                }

                // Write tree overlay.
                auto overlay_tree_info = WriteTree(api, overlay_tree);
                if (not overlay_tree_info) {
                    (*new_logger)(overlay_tree_info.error(), /*fatal=*/true);
                    return;
                }

                Logger::Log(LogLevel::Trace,
                            "Tree-overlay result: {}",
                            overlay_tree_info->ToString());

                (*setter)(*std::move(overlay_tree_info));
            },
            new_logger);
    };

    return AsyncMapConsumer<TreePair, Artifact::ObjectInfo>{value_creator};
}

auto TreeOperationsUtils::ComputeTreeOverlay(
    IExecutionApi const& api,
    Artifact::ObjectInfo const& base_tree_info,
    Artifact::ObjectInfo const& other_tree_info,
    bool disjoint) noexcept -> expected<Artifact::ObjectInfo, std::string> {

    auto tree_overlay_map = CreateTreeOverlayMap(api, disjoint);
    Artifact::ObjectInfo result{};
    std::string failed_msg{};
    bool failed{false};
    {
        TaskSystem ts{1};
        tree_overlay_map.ConsumeAfterKeysReady(
            &ts,
            {TreePair{std::make_pair(base_tree_info, other_tree_info)}},
            [&result](auto const& values) { result = *values[0]; },
            [&failed_msg, &failed](auto const& msg, auto fatal) {
                failed_msg = msg;
                failed = fatal;
            });
    }
    if (failed) {
        return unexpected{failed_msg};
    }
    return result;
}
