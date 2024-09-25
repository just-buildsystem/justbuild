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

#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"

#include <algorithm>
#include <compare>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"

namespace {
/// \brief Serialize protobuf message to string.
template <class T>
[[nodiscard]] auto SerializeMessage(T const& message) noexcept
    -> std::optional<std::string> {
    try {
        std::string content(message.ByteSizeLong(), '\0');
        message.SerializeToArray(content.data(),
                                 gsl::narrow<int>(content.size()));
        return content;
    } catch (...) {
        return std::nullopt;
    }
}

/// \brief Create protobuf message 'Platform'.
[[nodiscard]] auto CreatePlatform(
    std::vector<bazel_re::Platform_Property> const& props) noexcept
    -> std::unique_ptr<bazel_re::Platform> {
    auto platform = std::make_unique<bazel_re::Platform>();
    std::copy(props.cbegin(),
              props.cend(),
              pb::back_inserter(platform->mutable_properties()));
    return platform;
}

/// \brief Create protobuf message 'Directory'.
[[nodiscard]] auto CreateDirectory(
    std::vector<bazel_re::FileNode> const& files,
    std::vector<bazel_re::DirectoryNode> const& dirs,
    std::vector<bazel_re::SymlinkNode> const& links) noexcept
    -> bazel_re::Directory {
    bazel_re::Directory dir{};

    auto copy_nodes = [](auto* pb_container, auto const& nodes) {
        pb_container->Reserve(gsl::narrow<int>(nodes.size()));
        std::copy(nodes.begin(), nodes.end(), pb::back_inserter(pb_container));
        std::sort(
            pb_container->begin(),
            pb_container->end(),
            [](auto const& l, auto const& r) { return l.name() < r.name(); });
    };

    copy_nodes(dir.mutable_files(), files);
    copy_nodes(dir.mutable_directories(), dirs);
    copy_nodes(dir.mutable_symlinks(), links);

    return dir;
}

/// \brief Create protobuf message 'FileNode'.
[[nodiscard]] auto CreateFileNode(std::string const& file_name,
                                  ObjectType type,
                                  ArtifactDigest const& digest) noexcept
    -> bazel_re::FileNode {
    bazel_re::FileNode node;
    node.set_name(file_name);
    node.set_is_executable(IsExecutableObject(type));
    (*node.mutable_digest()) = ArtifactDigestFactory::ToBazel(digest);
    return node;
}

/// \brief Create protobuf message 'DirectoryNode'.
[[nodiscard]] auto CreateDirectoryNode(std::string const& dir_name,
                                       ArtifactDigest const& digest) noexcept
    -> bazel_re::DirectoryNode {
    bazel_re::DirectoryNode node;
    node.set_name(dir_name);
    (*node.mutable_digest()) = ArtifactDigestFactory::ToBazel(digest);
    return node;
}

/// \brief Create protobuf message 'SymlinkNode'.
[[nodiscard]] auto CreateSymlinkNode(std::string const& link_name,
                                     std::string const& target) noexcept
    -> bazel_re::SymlinkNode {
    bazel_re::SymlinkNode node;
    node.set_name(link_name);
    node.set_target(target);
    return node;
}

/// \brief Create protobuf message SymlinkNode from Digest for multiple
/// instances at once
[[nodiscard]] auto CreateSymlinkNodesFromDigests(
    std::vector<std::string> const& symlink_names,
    std::vector<ArtifactDigest> const& symlink_digests,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links)
    -> std::vector<bazel_re::SymlinkNode> {
    std::vector<std::string> symlink_targets;
    resolve_links(symlink_digests, &symlink_targets);
    auto it_name = symlink_names.begin();
    auto it_target = symlink_targets.begin();
    std::vector<bazel_re::SymlinkNode> symlink_nodes;
    // both loops have same length
    for (; it_name != symlink_names.end(); ++it_name, ++it_target) {
        symlink_nodes.emplace_back(CreateSymlinkNode(*it_name, *it_target));
    }
    return symlink_nodes;
}

struct DirectoryNodeBundle final {
    bazel_re::DirectoryNode message;
    ArtifactBlob blob;
};

/// \brief Create bundle for protobuf message DirectoryNode from Directory.
[[nodiscard]] auto CreateDirectoryNodeBundle(std::string const& dir_name,
                                             bazel_re::Directory const& dir)
    -> std::optional<DirectoryNodeBundle> {
    auto content = SerializeMessage(dir);
    if (not content) {
        return std::nullopt;
    }

    // SHA256 is used since bazel types are processed here.
    HashFunction const hash_function{HashFunction::Type::PlainSHA256};
    auto digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, *content);

    return DirectoryNodeBundle{
        .message = CreateDirectoryNode(dir_name, digest),
        .blob = ArtifactBlob{
            std::move(digest), std::move(*content), /*is_exec=*/false}};
}

/// \brief Create bundle for protobuf message Command from args strings.
[[nodiscard]] auto CreateCommandBundle(
    BazelMsgFactory::ActionDigestRequest const& request)
    -> std::optional<ArtifactBlob> {
    bazel_re::Command msg;
    // DEPRECATED as of v2.2: platform properties are now specified
    // directly in the action. See documentation note in the
    // [Action][build.bazel.remote.execution.v2.Action] for migration.
    // (https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L646)
    msg.set_allocated_platform(CreatePlatform(*request.properties).release());
    msg.set_working_directory(*request.cwd);
    std::copy(request.command_line->begin(),
              request.command_line->end(),
              pb::back_inserter(msg.mutable_arguments()));
    std::copy(request.output_files->begin(),
              request.output_files->end(),
              pb::back_inserter(msg.mutable_output_files()));
    std::copy(request.output_dirs->begin(),
              request.output_dirs->end(),
              pb::back_inserter(msg.mutable_output_directories()));
    std::copy(request.env_vars->begin(),
              request.env_vars->end(),
              pb::back_inserter(msg.mutable_environment_variables()));

    auto content = SerializeMessage(msg);
    if (not content) {
        return std::nullopt;
    }
    auto digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        request.hash_function, *content);
    return ArtifactBlob{std::move(digest),
                        std::move(*content),
                        /*is_exec=*/false};
}

/// \brief Create bundle for protobuf message Action from Command.
[[nodiscard]] auto CreateActionBundle(
    ArtifactDigest const& command,
    BazelMsgFactory::ActionDigestRequest const& request)
    -> std::optional<ArtifactBlob> {
    using seconds = std::chrono::seconds;
    using nanoseconds = std::chrono::nanoseconds;
    auto sec = std::chrono::duration_cast<seconds>(request.timeout);
    auto nanos = std::chrono::duration_cast<nanoseconds>(request.timeout - sec);

    auto duration = std::make_unique<google::protobuf::Duration>();
    duration->set_seconds(sec.count());
    duration->set_nanos(static_cast<int>(nanos.count()));

    bazel_re::Action msg;
    msg.set_do_not_cache(request.skip_action_cache);
    msg.set_allocated_timeout(duration.release());
    *msg.mutable_command_digest() = ArtifactDigestFactory::ToBazel(command);
    *msg.mutable_input_root_digest() =
        ArtifactDigestFactory::ToBazel(*request.exec_dir);

    // New in version 2.2: clients SHOULD set these platform properties
    // as well as those in the
    // [Command][build.bazel.remote.execution.v2.Command]. Servers
    // SHOULD prefer those set here.
    // (https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L516)
    msg.set_allocated_platform(CreatePlatform(*request.properties).release());

    auto content = SerializeMessage(msg);
    if (not content) {
        return std::nullopt;
    }
    auto digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        request.hash_function, *content);
    return ArtifactBlob{std::move(digest),
                        std::move(*content),
                        /*is_exec=*/false};
}

/// \brief Convert `DirectoryTree` to `DirectoryNodeBundle`.
[[nodiscard]] auto DirectoryTreeToBundle(
    std::string const& root_name,
    DirectoryTreePtr const& tree,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links,
    BazelMsgFactory::BlobProcessFunc const& process_blob,
    std::filesystem::path const& parent = "") noexcept
    -> std::optional<DirectoryNodeBundle> {
    std::vector<bazel_re::FileNode> file_nodes{};
    std::vector<bazel_re::DirectoryNode> dir_nodes{};
    std::vector<std::string> symlink_names{};
    std::vector<ArtifactDigest> symlink_digests{};
    try {
        for (auto const& [name, node] : *tree) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto dir_bundle = DirectoryTreeToBundle(
                    name, dir, resolve_links, process_blob, parent / name);
                if (not dir_bundle) {
                    return std::nullopt;
                }
                dir_nodes.emplace_back(std::move(dir_bundle->message));
                if (not process_blob(std::move(dir_bundle->blob))) {
                    return std::nullopt;
                }
            }
            else {
                auto const& artifact = std::get<Artifact const*>(node);
                auto const& object_info = artifact->Info();
                if (not object_info) {
                    return std::nullopt;
                }
                if (IsTreeObject(object_info->type)) {
                    dir_nodes.emplace_back(
                        CreateDirectoryNode(name, object_info->digest));
                }
                else if (IsSymlinkObject(object_info->type)) {
                    // for symlinks we need to retrieve the data from the
                    // digest, which we will handle in bulk
                    symlink_names.emplace_back(name);
                    symlink_digests.emplace_back(object_info->digest);
                }
                else {
                    file_nodes.emplace_back(CreateFileNode(
                        name, object_info->type, object_info->digest));
                }
            }
        }
        return CreateDirectoryNodeBundle(
            root_name,
            CreateDirectory(
                file_nodes,
                dir_nodes,
                CreateSymlinkNodesFromDigests(
                    symlink_names, symlink_digests, resolve_links)));
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] auto GetContentFromGitEntry(
    BazelMsgFactory::GitReadFunc const& read_git,
    ArtifactDigest const& digest,
    ObjectType entry_type) -> expected<std::string, std::string> {
    auto read_git_res = read_git(digest, entry_type);
    if (not read_git_res) {
        return unexpected{
            fmt::format("failed reading Git entry {}", digest.hash())};
    }
    if (std::holds_alternative<std::string>(read_git_res.value())) {
        return std::get<std::string>(std::move(read_git_res).value());
    }
    if (std::holds_alternative<std::filesystem::path>(read_git_res.value())) {
        auto content = FileSystemManager::ReadFile(
            std::get<std::filesystem::path>(std::move(read_git_res).value()));
        if (not content) {
            return unexpected{fmt::format("failed reading content of tree {}",
                                          digest.hash())};
        }
        return *std::move(content);
    }
    return unexpected{
        fmt::format("unexpected failure reading Git entry {}", digest.hash())};
}

}  // namespace

auto BazelMsgFactory::CreateDirectoryDigestFromTree(
    DirectoryTreePtr const& tree,
    LinkDigestResolveFunc const& resolve_links,
    BlobProcessFunc const& process_blob) noexcept
    -> std::optional<ArtifactDigest> {
    auto bundle = DirectoryTreeToBundle("", tree, resolve_links, process_blob);
    if (not bundle) {
        return std::nullopt;
    }

    auto digest = bundle->blob.digest;
    try {
        if (not process_blob(std::move(bundle->blob))) {
            return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
    return digest;
}

auto BazelMsgFactory::CreateDirectoryDigestFromGitTree(
    ArtifactDigest const& digest,
    GitReadFunc const& read_git,
    BlobStoreFunc const& store_file,
    TreeStoreFunc const& store_dir,
    SymlinkStoreFunc const& store_symlink,
    RehashedDigestReadFunc const& read_rehashed,
    RehashedDigestStoreFunc const& store_rehashed) noexcept
    -> expected<ArtifactDigest, std::string> {
    std::vector<bazel_re::FileNode> files{};
    std::vector<bazel_re::DirectoryNode> dirs{};
    std::vector<bazel_re::SymlinkNode> symlinks{};

    try {
        // read tree object
        auto const tree_content =
            GetContentFromGitEntry(read_git, digest, ObjectType::Tree);
        if (not tree_content) {
            return unexpected{tree_content.error()};
        }
        auto const check_symlinks =
            [&read_git](std::vector<ArtifactDigest> const& ids) {
                return std::all_of(ids.begin(),
                                   ids.end(),
                                   [&read_git](auto const& id) -> bool {
                                       auto content = GetContentFromGitEntry(
                                           read_git, id, ObjectType::Symlink);
                                       return content and
                                              PathIsNonUpwards(*content);
                                   });
            };

        // Git-SHA1 hashing is used for reading from git
        HashFunction const hash_function{HashFunction::Type::GitSHA1};
        // the tree digest is in native mode, so no need for rehashing content
        auto const entries = GitRepo::ReadTreeData(
            *tree_content, digest.hash(), check_symlinks, /*is_hex_id=*/true);
        if (not entries) {
            return unexpected{fmt::format("failed reading entries of tree {}",
                                          digest.hash())};
        }

        // handle tree entries
        for (auto const& [raw_id, es] : *entries) {
            auto const hex_id = ToHexString(raw_id);
            for (auto const& entry : es) {
                // get native digest of entry
                auto const git_digest =
                    ArtifactDigestFactory::Create(HashFunction::Type::GitSHA1,
                                                  hex_id,
                                                  /*size is unknown*/ 0,
                                                  IsTreeObject(entry.type));
                if (not git_digest) {
                    return unexpected{git_digest.error()};
                }
                // get any cached digest mapping, to avoid unnecessary work
                auto const cached_obj = read_rehashed(*git_digest);
                if (not cached_obj) {
                    return unexpected{cached_obj.error()};
                }
                // create and store the directory entry
                switch (entry.type) {
                    case ObjectType::Tree: {
                        if (cached_obj.value()) {
                            // no work to be done if we already know the digest
                            dirs.emplace_back(CreateDirectoryNode(
                                entry.name, cached_obj.value()->digest));
                        }
                        else {
                            // create and store sub directory
                            auto const dir_digest =
                                CreateDirectoryDigestFromGitTree(
                                    *git_digest,
                                    read_git,
                                    store_file,
                                    store_dir,
                                    store_symlink,
                                    read_rehashed,
                                    store_rehashed);
                            if (not dir_digest) {
                                return unexpected{dir_digest.error()};
                            }
                            dirs.emplace_back(
                                CreateDirectoryNode(entry.name, *dir_digest));
                            // no need to cache the digest mapping, as this was
                            // done in the recursive call
                        }
                    } break;
                    case ObjectType::Symlink: {
                        // create and store symlink; for this entry type the
                        // cached digest is ignored because we always need the
                        // target (i.e., the symlink content)
                        auto const sym_target = GetContentFromGitEntry(
                            read_git, *git_digest, ObjectType::Symlink);
                        if (not sym_target) {
                            return unexpected{sym_target.error()};
                        }
                        auto const sym_digest = store_symlink(*sym_target);
                        if (not sym_digest) {
                            return unexpected{fmt::format(
                                "failed storing symlink {}", hex_id)};
                        }
                        symlinks.emplace_back(
                            CreateSymlinkNode(entry.name, *sym_target));
                        // while useless for future symlinks, cache digest
                        // mapping for file-type blobs with same content
                        if (auto error_msg =
                                store_rehashed(*git_digest,
                                               *sym_digest,
                                               ObjectType::Symlink)) {
                            return unexpected{*std::move(error_msg)};
                        }
                    } break;
                    default: {
                        if (cached_obj.value()) {
                            // no work to be done if we already know the digest
                            files.emplace_back(
                                CreateFileNode(entry.name,
                                               entry.type,
                                               cached_obj.value()->digest));
                        }
                        else {
                            // create and store file; here we want to NOT read
                            // the content if from CAS, where we can rehash via
                            // streams!
                            auto const read_git_file =
                                read_git(*git_digest, entry.type);
                            if (not read_git_file) {
                                return unexpected{
                                    fmt::format("failed reading Git entry ")};
                            }
                            auto const file_digest = store_file(
                                *read_git_file, IsExecutableObject(entry.type));
                            if (not file_digest) {
                                return unexpected{fmt::format(
                                    "failed storing file {}", hex_id)};
                            }
                            files.emplace_back(CreateFileNode(
                                entry.name, entry.type, *file_digest));
                            // cache digest mapping
                            if (auto error_msg = store_rehashed(
                                    *git_digest, *file_digest, entry.type)) {
                                return unexpected{*std::move(error_msg)};
                            }
                        }
                    }
                }
            }
        }

        // create and store tree
        auto const bytes =
            SerializeMessage(CreateDirectory(files, dirs, symlinks));
        if (not bytes) {
            return unexpected{
                fmt::format("failed serializing bazel Directory for tree {}",
                            digest.hash())};
        }
        auto const tree_digest = store_dir(*bytes);
        if (not tree_digest) {
            return unexpected{fmt::format(
                "failed storing bazel Directory for tree {}", digest.hash())};
        }
        // cache digest mapping
        if (auto error_msg =
                store_rehashed(digest, *tree_digest, ObjectType::Tree)) {
            return unexpected{*std::move(error_msg)};
        }
        // return digest
        return *tree_digest;
    } catch (std::exception const& ex) {
        return unexpected{fmt::format(
            "creating bazel Directory digest unexpectedly failed with:\n{}",
            ex.what())};
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
auto BazelMsgFactory::CreateGitTreeDigestFromDirectory(
    ArtifactDigest const& digest,
    PathReadFunc const& read_path,
    FileStoreFunc const& store_file,
    TreeStoreFunc const& store_tree,
    SymlinkStoreFunc const& store_symlink,
    RehashedDigestReadFunc const& read_rehashed,
    RehashedDigestStoreFunc const& store_rehashed) noexcept
    -> expected<ArtifactDigest, std::string> {
    GitRepo::tree_entries_t entries{};

    try {
        // read directory object
        auto const tree_path = read_path(digest, ObjectType::Tree);
        if (not tree_path) {
            return unexpected{
                fmt::format("failed reading CAS entry {}", digest.hash())};
        }
        auto const tree_content = FileSystemManager::ReadFile(*tree_path);
        if (not tree_content) {
            return unexpected{fmt::format("failed reading content of tree {}",
                                          digest.hash())};
        }
        auto dir = BazelMsgFactory::MessageFromString<bazel_re::Directory>(
            *tree_content);

        // process subdirectories
        for (auto const& subdir : dir->directories()) {
            // get digest
            auto const subdir_digest = ArtifactDigestFactory::FromBazel(
                HashFunction::Type::PlainSHA256, subdir.digest());
            if (not subdir_digest) {
                return unexpected{subdir_digest.error()};
            }
            // get any cached digest mapping, to avoid unnecessary work
            auto const cached_obj = read_rehashed(*subdir_digest);
            if (not cached_obj) {
                return unexpected{cached_obj.error()};
            }
            if (cached_obj.value()) {
                // no work to be done, just add to map
                if (auto raw_id =
                        FromHexString(cached_obj.value()->digest.hash())) {
                    entries[std::move(*raw_id)].emplace_back(subdir.name(),
                                                             ObjectType::Tree);
                }
                else {
                    return unexpected{fmt::format(
                        "failed to get raw id for cached dir digest {}",
                        cached_obj.value()->digest.hash())};
                }
            }
            else {
                // recursively get the subdirectory digest
                auto const tree_digest =
                    CreateGitTreeDigestFromDirectory(*subdir_digest,
                                                     read_path,
                                                     store_file,
                                                     store_tree,
                                                     store_symlink,
                                                     read_rehashed,
                                                     store_rehashed);
                if (not tree_digest) {
                    return unexpected{tree_digest.error()};
                }
                if (auto raw_id = FromHexString(tree_digest->hash())) {
                    entries[std::move(*raw_id)].emplace_back(subdir.name(),
                                                             ObjectType::Tree);
                }
                else {
                    return unexpected{
                        fmt::format("failed to get raw id for tree digest {}",
                                    tree_digest->hash())};
                }
                // no need to cache the digest mapping, as this was done in the
                // recursive call
            }
        }

        // process symlinks
        for (auto const& sym : dir->symlinks()) {
            // get digest
            auto const sym_digest =
                ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                    HashFunction{HashFunction::Type::PlainSHA256},
                    sym.target());
            // get any cached digest mapping, to avoid unnecessary work
            auto const cached_obj = read_rehashed(sym_digest);
            if (not cached_obj) {
                return unexpected{cached_obj.error()};
            }
            if (cached_obj.value()) {
                // no work to be done, just add to map
                if (auto raw_id =
                        FromHexString(cached_obj.value()->digest.hash())) {
                    entries[std::move(*raw_id)].emplace_back(
                        sym.name(), ObjectType::Symlink);
                }
                else {
                    return unexpected{fmt::format(
                        "failed to get raw id for cached symlink digest {}",
                        cached_obj.value()->digest.hash())};
                }
            }
            else {
                // check validity of symlink
                if (not PathIsNonUpwards(sym.target())) {
                    return unexpected{fmt::format(
                        "found non-upwards symlink {}", sym_digest.hash())};
                }
                // rehash symlink
                auto const blob_digest = store_symlink(sym.target());
                if (not blob_digest) {
                    return unexpected{
                        fmt::format("failed rehashing as blob symlink {}",
                                    sym_digest.hash())};
                }
                if (auto raw_id = FromHexString(blob_digest->hash())) {
                    entries[std::move(*raw_id)].emplace_back(
                        sym.name(), ObjectType::Symlink);
                }
                else {
                    return unexpected{fmt::format(
                        "failed to get raw id for symlink blob digest {}",
                        blob_digest->hash())};
                }
                // while useless for future symlinks, cache digest mapping for
                // file-type blobs with same content
                if (auto error_msg = store_rehashed(
                        sym_digest, *blob_digest, ObjectType::Symlink)) {
                    return unexpected{*std::move(error_msg)};
                }
            }
        }

        // process files
        for (auto const& file : dir->files()) {
            // get digest
            auto const file_digest = ArtifactDigestFactory::FromBazel(
                HashFunction::Type::PlainSHA256, file.digest());
            if (not file_digest) {
                return unexpected{file_digest.error()};
            }
            auto const file_type = file.is_executable() ? ObjectType::Executable
                                                        : ObjectType::File;
            // get any cached digest mapping, to avoid unnecessary work
            auto const cached_obj = read_rehashed(*file_digest);
            if (not cached_obj) {
                return unexpected{cached_obj.error()};
            }
            if (cached_obj.value()) {
                // no work to be done, just add to map
                if (auto raw_id =
                        FromHexString(cached_obj.value()->digest.hash())) {
                    entries[std::move(*raw_id)].emplace_back(file.name(),
                                                             file_type);
                }
                else {
                    return unexpected{fmt::format(
                        "failed to get raw id for cached file digest {}",
                        cached_obj.value()->digest.hash())};
                }
            }
            else {
                // rehash file
                auto const file_path = read_path(*file_digest, file_type);
                if (not file_path) {
                    return unexpected{fmt::format("failed reading CAS entry {}",
                                                  file_digest->hash())};
                }
                auto const blob_digest =
                    store_file(*file_path, file.is_executable());
                if (not blob_digest) {
                    return unexpected{
                        fmt::format("failed rehashing as blob file {}",
                                    file_digest->hash())};
                }
                if (auto raw_id = FromHexString(blob_digest->hash())) {
                    entries[std::move(*raw_id)].emplace_back(file.name(),
                                                             file_type);
                }
                else {
                    return unexpected{fmt::format(
                        "failed to get raw id for file blob digest {}",
                        blob_digest->hash())};
                }
                // cache digest mapping
                if (auto error_msg =
                        store_rehashed(*file_digest, *blob_digest, file_type)) {
                    return unexpected{*std::move(error_msg)};
                }
            }
        }

        // create and store Git tree
        auto const git_tree = GitRepo::CreateShallowTree(entries);
        if (not git_tree) {
            return unexpected{
                fmt::format("failed creating Git tree for bazel Directory {}",
                            digest.hash())};
        }
        auto const tree_digest = store_tree(git_tree->second);
        // cache digest mapping
        if (auto error_msg =
                store_rehashed(digest, *tree_digest, ObjectType::Tree)) {
            return unexpected{*std::move(error_msg)};
        }
        // return digest
        return *tree_digest;
    } catch (std::exception const& ex) {
        return unexpected{fmt::format(
            "creating Git tree digest unexpectedly failed with:\n{}",
            ex.what())};
    }
}

auto BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
    std::filesystem::path const& root,
    FileStoreFunc const& store_file,
    TreeStoreFunc const& store_dir,
    SymlinkStoreFunc const& store_symlink) noexcept
    -> std::optional<ArtifactDigest> {
    std::vector<bazel_re::FileNode> files{};
    std::vector<bazel_re::DirectoryNode> dirs{};
    std::vector<bazel_re::SymlinkNode> symlinks{};

    auto dir_reader = [&files,
                       &dirs,
                       &symlinks,
                       &root,
                       &store_file,
                       &store_dir,
                       &store_symlink](auto name, auto type) {
        const auto full_name = root / name;
        if (IsTreeObject(type)) {
            // create and store sub directory
            auto digest = CreateDirectoryDigestFromLocalTree(
                root / name, store_file, store_dir, store_symlink);
            if (not digest) {
                Logger::Log(LogLevel::Error,
                            "failed storing tree {}",
                            full_name.string());
                return false;
            }

            dirs.emplace_back(CreateDirectoryNode(name.string(), *digest));
            return true;
        }

        try {
            if (IsSymlinkObject(type)) {
                // create and store symlink
                auto content = FileSystemManager::ReadSymlink(full_name);
                if (content and store_symlink(*content)) {
                    symlinks.emplace_back(
                        CreateSymlinkNode(name.string(), *content));
                    return true;
                }
                Logger::Log(LogLevel::Error,
                            "failed storing symlink {}",
                            full_name.string());
                return false;
            }
            // create and store file
            if (auto digest = store_file(full_name, IsExecutableObject(type))) {
                auto file = CreateFileNode(name.string(), type, *digest);
                files.emplace_back(std::move(file));
                return true;
            }
            Logger::Log(
                LogLevel::Error, "failed storing file {}", full_name.string());
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "storing file failed with:\n{}", ex.what());
        }
        return false;
    };

    if (FileSystemManager::ReadDirectory(
            root, dir_reader, /*allow_upwards=*/true)) {
        auto dir = CreateDirectory(files, dirs, symlinks);
        if (auto bytes = SerializeMessage(dir)) {
            try {
                return store_dir(*bytes);
            } catch (std::exception const& ex) {
                Logger::Log(LogLevel::Error,
                            "storing directory failed with:\n{}",
                            ex.what());
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

auto BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
    std::filesystem::path const& root,
    FileStoreFunc const& store_file,
    TreeStoreFunc const& store_tree,
    SymlinkStoreFunc const& store_symlink) noexcept
    -> std::optional<ArtifactDigest> {
    GitRepo::tree_entries_t entries{};
    auto dir_reader = [&entries,
                       &root,
                       &store_file,
                       &store_tree,
                       &store_symlink](auto name, auto type) {
        const auto full_name = root / name;
        if (IsTreeObject(type)) {
            // create and store sub directory
            if (auto digest = CreateGitTreeDigestFromLocalTree(
                    full_name, store_file, store_tree, store_symlink)) {
                if (auto raw_id = FromHexString(digest->hash())) {
                    entries[std::move(*raw_id)].emplace_back(name.string(),
                                                             ObjectType::Tree);
                    return true;
                }
            }
            Logger::Log(
                LogLevel::Error, "failed storing tree {}", full_name.string());
            return false;
        }

        try {
            if (IsSymlinkObject(type)) {
                auto content = FileSystemManager::ReadSymlink(full_name);
                if (content and PathIsNonUpwards(*content)) {
                    if (auto digest = store_symlink(*content)) {
                        if (auto raw_id = FromHexString(digest->hash())) {
                            entries[std::move(*raw_id)].emplace_back(
                                name.string(), type);
                            return true;
                        }
                    }
                    Logger::Log(LogLevel::Error,
                                "failed storing symlink {}",
                                full_name.string());
                }
                else {
                    Logger::Log(LogLevel::Error,
                                "failed storing symlink {} -- not non-upwards",
                                full_name.string());
                }
                return false;
            }
            // create and store file
            if (auto digest = store_file(full_name, IsExecutableObject(type))) {
                if (auto raw_id = FromHexString(digest->hash())) {
                    entries[std::move(*raw_id)].emplace_back(name.string(),
                                                             type);
                    return true;
                }
            }
            Logger::Log(
                LogLevel::Error, "failed storing file {}", full_name.string());
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "storing file failed with:\n{}", ex.what());
        }
        return false;
    };

    if (FileSystemManager::ReadDirectory(
            root, dir_reader, /*allow_upwards=*/true)) {
        if (auto tree = GitRepo::CreateShallowTree(entries)) {
            try {
                return store_tree(tree->second);
            } catch (std::exception const& ex) {
                Logger::Log(LogLevel::Error,
                            "storing tree failed with:\n{}",
                            ex.what());
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

auto BazelMsgFactory::CreateActionDigestFromCommandLine(
    ActionDigestRequest const& request) -> std::optional<ArtifactDigest> {
    auto cmd = CreateCommandBundle(request);
    if (not cmd) {
        return std::nullopt;
    }

    auto action = CreateActionBundle(cmd->digest, request);
    if (not action) {
        return std::nullopt;
    }

    if (request.store_blob) {
        std::invoke(*request.store_blob,
                    BazelBlob{ArtifactDigestFactory::ToBazel(cmd->digest),
                              cmd->data,
                              cmd->is_exec});
        std::invoke(*request.store_blob,
                    BazelBlob{ArtifactDigestFactory::ToBazel(action->digest),
                              action->data,
                              action->is_exec});
    }
    return action->digest;
}
