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

#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {

/// \brief Abstract interface for bundle (message, content, and digest).
/// Provides getters for content, corresponding digest, and creating a blob.
class IBundle {
  public:
    using Ptr = std::unique_ptr<IBundle>;
    using ContentCreateFunc = std::function<std::optional<std::string>()>;
    using DigestCreateFunc =
        std::function<bazel_re::Digest(std::string const&)>;

    IBundle() = default;
    IBundle(IBundle const&) = delete;
    IBundle(IBundle&&) = delete;
    auto operator=(IBundle const&) -> IBundle& = delete;
    auto operator=(IBundle&&) -> IBundle& = delete;
    virtual ~IBundle() noexcept = default;

    [[nodiscard]] virtual auto Content() const& noexcept
        -> std::string const& = 0;
    [[nodiscard]] virtual auto Digest() const& noexcept
        -> bazel_re::Digest const& = 0;
    [[nodiscard]] auto MakeBlob(bool is_exec) const noexcept -> BazelBlob {
        return BazelBlob{Digest(), Content(), is_exec};
    }
};

/// \brief Sparse Bundle implementation for protobuf messages.
/// It is called "Sparse" as it does not contain its own Digest. Instead, the
/// protobuf message's Digest is used.
/// \tparam T The actual protobuf message type.
template <typename T>
class SparseBundle final : public IBundle {
  public:
    using Ptr = std::unique_ptr<SparseBundle<T>>;

    [[nodiscard]] auto Message() const noexcept -> T const& { return msg_; }

    [[nodiscard]] auto Content() const& noexcept -> std::string const& final {
        return content_;
    }

    [[nodiscard]] auto Digest() const& noexcept
        -> bazel_re::Digest const& final {
        return msg_.digest();
    }

    [[nodiscard]] static auto Create(T const& msg,
                                     ContentCreateFunc const& content_creator,
                                     DigestCreateFunc const& digest_creator)
        -> Ptr {
        auto content = content_creator();
        if (content) {
            // create bundle with message and content
            Ptr bundle{new SparseBundle<T>{msg, std::move(*content)}};

            // create digest
            bundle->msg_.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
                new bazel_re::Digest{digest_creator(bundle->content_)}});
            return bundle;
        }
        return Ptr{};
    }

    SparseBundle(SparseBundle const&) = delete;
    SparseBundle(SparseBundle&&) = delete;
    auto operator=(SparseBundle const&) -> SparseBundle& = delete;
    auto operator=(SparseBundle&&) -> SparseBundle& = delete;
    ~SparseBundle() noexcept final = default;

  private:
    T msg_{};               /**< Protobuf message */
    std::string content_{}; /**< Content the message's digest refers to */

    explicit SparseBundle(T msg, std::string&& content)
        : msg_{std::move(msg)}, content_{std::move(content)} {}
};

/// \brief Full Bundle implementation for protobuf messages.
/// Contains its own Digest memory, as the protobuf message does not contain
/// one itself.
/// \tparam T The actual protobuf message type.
template <typename T>
class FullBundle final : public IBundle {
  public:
    using Ptr = std::unique_ptr<FullBundle<T>>;

    [[nodiscard]] auto Message() const noexcept -> T const& { return msg_; }

    auto Content() const& noexcept -> std::string const& final {
        return content_;
    }

    auto Digest() const& noexcept -> bazel_re::Digest const& final {
        return digest_;
    }

    [[nodiscard]] static auto Create(T const& msg,
                                     ContentCreateFunc const& content_creator,
                                     DigestCreateFunc const& digest_creator)
        -> Ptr {
        auto content = content_creator();
        if (content) {
            // create bundle with message and content
            Ptr bundle{new FullBundle<T>{msg, std::move(*content)}};

            // create digest
            bundle->digest_ = digest_creator(bundle->content_);
            return bundle;
        }
        return Ptr{};
    }

    FullBundle(FullBundle const&) = delete;
    FullBundle(FullBundle&&) = delete;
    auto operator=(FullBundle const&) -> FullBundle& = delete;
    auto operator=(FullBundle&&) -> FullBundle& = delete;
    ~FullBundle() noexcept final = default;

  private:
    T msg_{};                   /**< Protobuf message */
    bazel_re::Digest digest_{}; /**< Digest of content */
    std::string content_{};     /**< Content the digest refers to */

    explicit FullBundle(T msg, std::string&& content)
        : msg_{std::move(msg)}, content_{std::move(content)} {}
};

using DirectoryNodeBundle = SparseBundle<bazel_re::DirectoryNode>;
using SymlinkNodeBundle = FullBundle<bazel_re::SymlinkNode>;
using ActionBundle = FullBundle<bazel_re::Action>;
using CommandBundle = FullBundle<bazel_re::Command>;

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
    -> DirectoryNodeBundle::Ptr {
    // setup protobuf message except digest
    auto msg = CreateDirectoryNode(dir_name);
    auto content_creator = [&dir] { return SerializeMessage(dir); };
    auto digest_creator = [](std::string const& content) -> bazel_re::Digest {
        return ArtifactDigest::Create<ObjectType::File>(content);
    };
    return DirectoryNodeBundle::Create(msg, content_creator, digest_creator);
}

/// \brief Create bundle for protobuf message Command from args strings.
[[nodiscard]] auto CreateCommandBundle(
    std::vector<std::string> const& args,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::vector<bazel_re::Command_EnvironmentVariable> const& env_vars,
    std::vector<bazel_re::Platform_Property> const& platform_properties)
    -> CommandBundle::Ptr {
    bazel_re::Command msg;
    // DEPRECATED as of v2.2: platform properties are now specified
    // directly in the action. See documentation note in the
    // [Action][build.bazel.remote.execution.v2.Action] for migration.
    // (https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L646)
    msg.set_allocated_platform(CreatePlatform(platform_properties).release());
    std::copy(std::cbegin(args),
              std::cend(args),
              pb::back_inserter(msg.mutable_arguments()));
    std::copy(std::cbegin(output_files),
              std::cend(output_files),
              pb::back_inserter(msg.mutable_output_files()));
    std::copy(std::cbegin(output_dirs),
              std::cend(output_dirs),
              pb::back_inserter(msg.mutable_output_directories()));
    std::copy(std::cbegin(env_vars),
              std::cend(env_vars),
              pb::back_inserter(msg.mutable_environment_variables()));

    auto content_creator = [&msg] { return SerializeMessage(msg); };

    auto digest_creator = [](std::string const& content) -> bazel_re::Digest {
        return ArtifactDigest::Create<ObjectType::File>(content);
    };

    return CommandBundle::Create(msg, content_creator, digest_creator);
}

/// \brief Create bundle for protobuf message Action from Command.
[[nodiscard]] auto CreateActionBundle(
    bazel_re::Digest const& command,
    bazel_re::Digest const& root_dir,
    std::vector<bazel_re::Platform_Property> const& platform_properties,
    bool do_not_cache,
    std::chrono::milliseconds const& timeout) -> ActionBundle::Ptr {
    using seconds = std::chrono::seconds;
    using nanoseconds = std::chrono::nanoseconds;
    auto sec = std::chrono::duration_cast<seconds>(timeout);
    auto nanos = std::chrono::duration_cast<nanoseconds>(timeout - sec);

    auto duration = std::make_unique<google::protobuf::Duration>();
    duration->set_seconds(sec.count());
    duration->set_nanos(nanos.count());

    bazel_re::Action msg;
    msg.set_do_not_cache(do_not_cache);
    msg.set_allocated_timeout(duration.release());
    msg.set_allocated_command_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{command}});
    msg.set_allocated_input_root_digest(
        gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{root_dir}});
    // New in version 2.2: clients SHOULD set these platform properties
    // as well as those in the
    // [Command][build.bazel.remote.execution.v2.Command]. Servers
    // SHOULD prefer those set here.
    // (https://github.com/bazelbuild/remote-apis/blob/e1fe21be4c9ae76269a5a63215bb3c72ed9ab3f0/build/bazel/remote/execution/v2/remote_execution.proto#L516)
    msg.set_allocated_platform(CreatePlatform(platform_properties).release());

    auto content_creator = [&msg] { return SerializeMessage(msg); };

    auto digest_creator = [](std::string const& content) -> bazel_re::Digest {
        return ArtifactDigest::Create<ObjectType::File>(content);
    };

    return ActionBundle::Create(msg, content_creator, digest_creator);
}

[[nodiscard]] auto CreateObjectInfo(bazel_re::DirectoryNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{.digest = ArtifactDigest{node.digest()},
                                .type = ObjectType::Tree};
}

[[nodiscard]] auto CreateObjectInfo(bazel_re::FileNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{.digest = ArtifactDigest{node.digest()},
                                .type = node.is_executable()
                                            ? ObjectType::Executable
                                            : ObjectType::File};
}

[[nodiscard]] auto CreateObjectInfo(bazel_re::SymlinkNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{
        ArtifactDigest::Create<ObjectType::File>(node.target()),
        ObjectType::Symlink};
}

/// \brief Convert `DirectoryTree` to `DirectoryNodeBundle`.
/// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto DirectoryTreeToBundle(
    std::string const& root_name,
    DirectoryTreePtr const& tree,
    BazelMsgFactory::LinkDigestResolveFunc const& resolve_links,
    std::optional<BazelMsgFactory::BlobProcessFunc> const& process_blob,
    std::optional<BazelMsgFactory::InfoStoreFunc> const& store_info,
    std::filesystem::path const& parent = "") noexcept
    -> DirectoryNodeBundle::Ptr {
    std::vector<bazel_re::FileNode> file_nodes{};
    std::vector<bazel_re::DirectoryNode> dir_nodes{};
    std::vector<std::string> symlink_names{};
    std::vector<bazel_re::Digest> symlink_digests{};
    try {
        for (auto const& [name, node] : *tree) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto const dir_bundle = DirectoryTreeToBundle(name,
                                                              dir,
                                                              resolve_links,
                                                              process_blob,
                                                              store_info,
                                                              parent / name);
                if (not dir_bundle) {
                    return nullptr;
                }
                dir_nodes.emplace_back(dir_bundle->Message());
                if (process_blob and not(*process_blob)(dir_bundle->MakeBlob(
                                         /*is_exec=*/false))) {
                    return nullptr;
                }
            }
            else {
                auto const& artifact = std::get<Artifact const*>(node);
                auto const& object_info = artifact->Info();
                if (not object_info) {
                    return nullptr;
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
                if (store_info and
                    not(*store_info)(parent / name, *object_info)) {
                    return nullptr;
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
        return nullptr;
    }
    return nullptr;
}

}  // namespace

auto BazelMsgFactory::ReadObjectInfosFromDirectory(
    bazel_re::Directory const& dir,
    InfoStoreFunc const& store_info) noexcept -> bool {
    try {
        for (auto const& f : dir.files()) {
            if (not store_info(f.name(), CreateObjectInfo(f))) {
                return false;
            }
        }
        for (auto const& l : dir.symlinks()) {
            if (not store_info(l.name(), CreateObjectInfo(l))) {
                return false;
            }
        }
        for (auto const& d : dir.directories()) {
            if (not store_info(d.name(), CreateObjectInfo(d))) {
                return false;
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "reading object infos from Directory failed with:\n{}",
                    ex.what());
        return false;
    }
    return true;
}

auto BazelMsgFactory::ReadObjectInfosFromGitTree(
    GitRepo::tree_entries_t const& entries,
    InfoStoreFunc const& store_info) noexcept -> bool {
    try {
        for (auto const& [raw_id, es] : entries) {
            auto const hex_id = ToHexString(raw_id);
            for (auto const& entry : es) {
                if (not store_info(
                        entry.name,
                        Artifact::ObjectInfo{
                            .digest = ArtifactDigest{hex_id,
                                                     /*size is unknown*/ 0,
                                                     IsTreeObject(entry.type)},
                            .type = entry.type})) {
                    return false;
                }
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "reading object infos from Git tree failed with:\n{}",
                    ex.what());
        return false;
    }
    return true;
}

auto BazelMsgFactory::CreateDirectoryDigestFromTree(
    DirectoryTreePtr const& tree,
    LinkDigestResolveFunc const& resolve_links,
    std::optional<BlobProcessFunc> const& process_blob,
    std::optional<InfoStoreFunc> const& store_info) noexcept
    -> std::optional<bazel_re::Digest> {
    if (auto bundle = DirectoryTreeToBundle(
            "", tree, resolve_links, process_blob, store_info)) {
        if (process_blob) {
            try {
                if (not(*process_blob)(bundle->MakeBlob(/*is_exec=*/false))) {
                    return std::nullopt;
                }
            } catch (...) {
                return std::nullopt;
            }
        }
        return bundle->Digest();
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
    std::vector<std::string> const& cmdline,
    bazel_re::Digest const& exec_dir,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::vector<bazel_re::Command_EnvironmentVariable> const& env_vars,
    std::vector<bazel_re::Platform_Property> const& properties,
    bool do_not_cache,
    std::chrono::milliseconds const& timeout,
    std::optional<BlobStoreFunc> const& store_blob) -> bazel_re::Digest {
    // create command
    auto cmd = CreateCommandBundle(
        cmdline, output_files, output_dirs, env_vars, properties);

    // create action
    auto action = CreateActionBundle(
        cmd->Digest(), exec_dir, properties, do_not_cache, timeout);

    if (store_blob) {
        (*store_blob)(cmd->MakeBlob(/*is_exec=*/false));
        (*store_blob)(action->MakeBlob(/*is_exec=*/false));
    }

    return action->Digest();
}

auto BazelMsgFactory::DirectoryToString(bazel_re::Directory const& dir) noexcept
    -> std::optional<std::string> {
    auto json = nlohmann::json::object();
    try {
        if (not BazelMsgFactory::ReadObjectInfosFromDirectory(
                dir, [&json](auto path, auto info) {
                    json[path.string()] = info.ToString();
                    return true;
                })) {
            Logger::Log(LogLevel::Error,
                        "reading object infos from Directory failed");
            return std::nullopt;
        }
        return json.dump(2) + "\n";
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "dumping Directory to string failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto BazelMsgFactory::GitTreeToString(
    GitRepo::tree_entries_t const& entries) noexcept
    -> std::optional<std::string> {
    auto json = nlohmann::json::object();
    try {
        if (not BazelMsgFactory::ReadObjectInfosFromGitTree(
                entries, [&json](auto path, auto info) {
                    json[path.string()] = info.ToString(/*size_unknown=*/true);
                    return true;
                })) {
            Logger::Log(LogLevel::Error,
                        "reading object infos from Directory failed");
            return std::nullopt;
        }
        return json.dump(2) + "\n";
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "dumping Directory to string failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}
