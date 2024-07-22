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
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
struct DirectoryNodeBundle final {
    bazel_re::DirectoryNode const message;
    BazelBlob const bazel_blob;
};

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
    }
    return std::nullopt;
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
    std::vector<bazel_re::SymlinkNode> const& links,
    std::vector<bazel_re::NodeProperty> const& props) noexcept
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

    std::copy(
        props.cbegin(),
        props.cend(),
        pb::back_inserter(dir.mutable_node_properties()->mutable_properties()));

    return dir;
}

/// \brief Create protobuf message 'FileNode' without digest.
[[nodiscard]] auto CreateFileNode(
    std::string const& file_name,
    ObjectType type,
    std::vector<bazel_re::NodeProperty> const& props) noexcept
    -> bazel_re::FileNode {
    bazel_re::FileNode node;
    node.set_name(file_name);
    node.set_is_executable(IsExecutableObject(type));
    std::copy(props.cbegin(),
              props.cend(),
              pb::back_inserter(
                  node.mutable_node_properties()->mutable_properties()));
    return node;
}

/// \brief Create protobuf message 'DirectoryNode' without digest.
[[nodiscard]] auto CreateDirectoryNode(std::string const& dir_name) noexcept
    -> bazel_re::DirectoryNode {
    bazel_re::DirectoryNode node;
    node.set_name(dir_name);
    return node;
}

/// \brief Create protobuf message 'SymlinkNode'.
[[nodiscard]] auto CreateSymlinkNode(
    std::string const& link_name,
    std::string const& target,
    std::vector<bazel_re::NodeProperty> const& props) noexcept
    -> bazel_re::SymlinkNode {
    bazel_re::SymlinkNode node;
    node.set_name(link_name);
    node.set_target(target);
    std::copy(props.cbegin(),
              props.cend(),
              pb::back_inserter(
                  node.mutable_node_properties()->mutable_properties()));
    return node;
}

/// \brief Create protobuf message FileNode from Artifact::ObjectInfo
[[nodiscard]] auto CreateFileNodeFromObjectInfo(
    std::string const& name,
    Artifact::ObjectInfo const& object_info) noexcept -> bazel_re::FileNode {
    auto file_node = CreateFileNode(name, object_info.type, {});

    file_node.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
        new bazel_re::Digest{object_info.digest}});

    return file_node;
}

/// \brief Create protobuf message DirectoryNode from Artifact::ObjectInfo
[[nodiscard]] auto CreateDirectoryNodeFromObjectInfo(
    std::string const& name,
    Artifact::ObjectInfo const& object_info) noexcept
    -> bazel_re::DirectoryNode {
    auto dir_node = CreateDirectoryNode(name);

    dir_node.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
        new bazel_re::Digest{object_info.digest}});

    return dir_node;
}

/// \brief Create protobuf message SymlinkNode from Digest for multiple
/// instances at once
[[nodiscard]] auto CreateSymlinkNodesFromDigests(
    std::vector<std::string> const& symlink_names,
    std::vector<bazel_re::Digest> const& symlink_digests,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links)
    -> std::vector<bazel_re::SymlinkNode> {
    std::vector<std::string> symlink_targets;
    resolve_links(symlink_digests, &symlink_targets);
    auto it_name = symlink_names.begin();
    auto it_target = symlink_targets.begin();
    std::vector<bazel_re::SymlinkNode> symlink_nodes;
    // both loops have same length
    for (; it_name != symlink_names.end(); ++it_name, ++it_target) {
        symlink_nodes.emplace_back(CreateSymlinkNode(*it_name, *it_target, {}));
    }
    return symlink_nodes;
}

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
    auto digest =
        ArtifactDigest::Create<ObjectType::File>(hash_function, *content);

    auto msg = CreateDirectoryNode(dir_name);
    msg.set_allocated_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{digest}});

    return DirectoryNodeBundle{
        .message = std::move(msg),
        .bazel_blob = BazelBlob{
            std::move(digest), std::move(*content), /*is_exec=*/false}};
}

/// \brief Create bundle for protobuf message Command from args strings.
[[nodiscard]] auto CreateCommandBundle(
    BazelMsgFactory::ActionDigestRequest const& request)
    -> std::optional<BazelBlob> {
    bazel_re::Command msg;
    // DEPRECATED as of v2.2: platform properties are now specified
    // directly in the action. See documentation note in the
    // [Action][build.bazel.remote.execution.v2.Action] for migration.
    // (https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L646)
    msg.set_allocated_platform(CreatePlatform(*request.properties).release());
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
    auto digest = ArtifactDigest::Create<ObjectType::File>(
        request.hash_function, *content);
    return BazelBlob{digest, std::move(*content), /*is_exec=*/false};
}

/// \brief Create bundle for protobuf message Action from Command.
[[nodiscard]] auto CreateActionBundle(
    bazel_re::Digest const& command,
    BazelMsgFactory::ActionDigestRequest const& request)
    -> std::optional<BazelBlob> {
    using seconds = std::chrono::seconds;
    using nanoseconds = std::chrono::nanoseconds;
    auto sec = std::chrono::duration_cast<seconds>(request.timeout);
    auto nanos = std::chrono::duration_cast<nanoseconds>(request.timeout - sec);

    auto duration = std::make_unique<google::protobuf::Duration>();
    duration->set_seconds(sec.count());
    duration->set_nanos(nanos.count());

    bazel_re::Action msg;
    msg.set_do_not_cache(request.skip_action_cache);
    msg.set_allocated_timeout(duration.release());
    msg.set_allocated_command_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{command}});
    msg.set_allocated_input_root_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{*request.exec_dir}});
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
    auto digest = ArtifactDigest::Create<ObjectType::File>(
        request.hash_function, *content);
    return BazelBlob{digest, std::move(*content), /*is_exec=*/false};
}

/// \brief Convert `DirectoryTree` to `DirectoryNodeBundle`.
/// NOLINTNEXTLINE(misc-no-recursion)
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
    std::vector<bazel_re::Digest> symlink_digests{};
    try {
        for (auto const& [name, node] : *tree) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto const dir_bundle = DirectoryTreeToBundle(
                    name, dir, resolve_links, process_blob, parent / name);
                if (not dir_bundle) {
                    return std::nullopt;
                }
                dir_nodes.emplace_back(dir_bundle->message);
                if (not process_blob(BazelBlob{dir_bundle->bazel_blob})) {
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
                        CreateDirectoryNodeFromObjectInfo(name, *object_info));
                }
                else if (IsSymlinkObject(object_info->type)) {
                    // for symlinks we need to retrieve the data from the
                    // digest, which we will handle in bulk
                    symlink_names.emplace_back(name);
                    symlink_digests.emplace_back(object_info->digest);
                }
                else {
                    file_nodes.emplace_back(
                        CreateFileNodeFromObjectInfo(name, *object_info));
                }
            }
        }
        return CreateDirectoryNodeBundle(
            root_name,
            CreateDirectory(file_nodes,
                            dir_nodes,
                            CreateSymlinkNodesFromDigests(
                                symlink_names, symlink_digests, resolve_links),
                            {}));
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace

auto BazelMsgFactory::CreateDirectoryDigestFromTree(
    DirectoryTreePtr const& tree,
    LinkDigestResolveFunc const& resolve_links,
    BlobProcessFunc const& process_blob) noexcept
    -> std::optional<bazel_re::Digest> {
    if (auto bundle =
            DirectoryTreeToBundle("", tree, resolve_links, process_blob)) {
        try {
            if (not process_blob(BazelBlob{bundle->bazel_blob})) {
                return std::nullopt;
            }
        } catch (...) {
            return std::nullopt;
        }
        return bundle->bazel_blob.digest;
    }
    return std::nullopt;
}

auto BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
    std::filesystem::path const& root,
    FileStoreFunc const& store_file,
    TreeStoreFunc const& store_dir,
    SymlinkStoreFunc const& store_symlink) noexcept
    -> std::optional<bazel_re::Digest> {
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

            auto dir = CreateDirectoryNode(name.string());
            dir.set_allocated_digest(
                gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{*digest}});
            dirs.emplace_back(std::move(dir));
            return true;
        }

        try {
            if (IsSymlinkObject(type)) {
                // create and store symlink
                auto content = FileSystemManager::ReadSymlink(full_name);
                if (content and store_symlink(*content)) {
                    symlinks.emplace_back(
                        CreateSymlinkNode(name.string(), *content, {}));
                    return true;
                }
                Logger::Log(LogLevel::Error,
                            "failed storing symlink {}",
                            full_name.string());
                return false;
            }
            // create and store file
            if (auto digest = store_file(full_name, IsExecutableObject(type))) {
                auto file = CreateFileNode(name.string(), type, {});
                file.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
                    new bazel_re::Digest{std::move(*digest)}});
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
        auto dir = CreateDirectory(files, dirs, symlinks, {});
        if (auto bytes = SerializeMessage(dir)) {
            try {
                if (auto digest = store_dir(*bytes)) {
                    return *digest;
                }
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
    -> std::optional<bazel_re::Digest> {
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
                if (auto raw_id = FromHexString(
                        NativeSupport::Unprefix(digest->hash()))) {
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
                if (content) {
                    if (auto digest = store_symlink(*content)) {
                        if (auto raw_id = FromHexString(
                                NativeSupport::Unprefix(digest->hash()))) {
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
                if (auto raw_id = FromHexString(
                        NativeSupport::Unprefix(digest->hash()))) {
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
                if (auto digest = store_tree(tree->second)) {
                    return *digest;
                }
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
    ActionDigestRequest const& request) -> std::optional<bazel_re::Digest> {
    auto cmd = CreateCommandBundle(request);
    if (not cmd) {
        return std::nullopt;
    }

    auto action = CreateActionBundle(cmd->digest, request);
    if (not action) {
        return std::nullopt;
    }

    if (not request.store_blob) {
        return action->digest;
    }

    auto digest = action->digest;
    std::invoke(*request.store_blob, std::move(*cmd));
    std::invoke(*request.store_blob, std::move(*action));
    return digest;
}
