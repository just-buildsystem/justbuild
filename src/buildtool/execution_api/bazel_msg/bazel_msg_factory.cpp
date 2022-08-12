#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_cas.hpp"
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
    [[nodiscard]] auto MakeBlob() const noexcept -> BazelBlob {
        return BazelBlob{Digest(), Content()};
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

    std::copy(props.cbegin(),
              props.cend(),
              pb::back_inserter(dir.mutable_node_properties()));

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
              pb::back_inserter(node.mutable_node_properties()));
    return node;
}

/// \brief Create protobuf message 'DirectoryNode' without digest.
[[nodiscard]] auto CreateDirectoryNode(std::string const& dir_name) noexcept
    -> bazel_re::DirectoryNode {
    bazel_re::DirectoryNode node;
    node.set_name(dir_name);
    return node;
}

/// \brief Create profobuf message FileNode from Artifact::ObjectInfo
[[nodiscard]] auto CreateFileNodeFromObjectInfo(
    std::string const& name,
    Artifact::ObjectInfo const& object_info) noexcept -> bazel_re::FileNode {
    auto file_node = CreateFileNode(name, object_info.type, {});

    file_node.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
        new bazel_re::Digest{object_info.digest}});

    return file_node;
}

/// \brief Create profobuf message DirectoryNode from Artifact::ObjectInfo
[[nodiscard]] auto CreateDirectoryNodeFromObjectInfo(
    std::string const& name,
    Artifact::ObjectInfo const& object_info) noexcept
    -> bazel_re::DirectoryNode {
    auto dir_node = CreateDirectoryNode(name);

    dir_node.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
        new bazel_re::Digest{object_info.digest}});

    return dir_node;
}

/// \brief Create bundle for profobuf message DirectoryNode from Directory.
[[nodiscard]] auto CreateDirectoryNodeBundle(std::string const& dir_name,
                                             bazel_re::Directory const& dir)
    -> DirectoryNodeBundle::Ptr {
    // setup protobuf message except digest
    auto msg = CreateDirectoryNode(dir_name);
    auto content_creator = [&dir] { return SerializeMessage(dir); };
    auto digest_creator = [](std::string const& content) -> bazel_re::Digest {
        return ArtifactDigest::Create(content);
    };
    return DirectoryNodeBundle::Create(msg, content_creator, digest_creator);
}

/// \brief Create bundle for profobuf message Command from args strings.
[[nodiscard]] auto CreateCommandBundle(
    std::vector<std::string> const& args,
    std::vector<std::string> const& output_files,
    std::vector<std::string> const& output_dirs,
    std::vector<bazel_re::Command_EnvironmentVariable> const& env_vars,
    std::vector<bazel_re::Platform_Property> const& platform_properties)
    -> CommandBundle::Ptr {
    bazel_re::Command msg;
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
        return ArtifactDigest::Create(content);
    };

    return CommandBundle::Create(msg, content_creator, digest_creator);
}

/// \brief Create bundle for profobuf message Action from Command.
[[nodiscard]] auto CreateActionBundle(
    bazel_re::Digest const& command,
    bazel_re::Digest const& root_dir,
    std::vector<std::string> const& output_node_properties,
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
    std::copy(output_node_properties.cbegin(),
              output_node_properties.cend(),
              pb::back_inserter(msg.mutable_output_node_properties()));

    auto content_creator = [&msg] { return SerializeMessage(msg); };

    auto digest_creator = [](std::string const& content) -> bazel_re::Digest {
        return ArtifactDigest::Create(content);
    };

    return ActionBundle::Create(msg, content_creator, digest_creator);
}

[[nodiscard]] auto CreateObjectInfo(bazel_re::DirectoryNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{ArtifactDigest{node.digest()},
                                ObjectType::Tree};
}

[[nodiscard]] auto CreateObjectInfo(bazel_re::FileNode const& node)
    -> Artifact::ObjectInfo {
    return Artifact::ObjectInfo{
        ArtifactDigest{node.digest()},
        node.is_executable() ? ObjectType::Executable : ObjectType::File};
}

class DirectoryTree;
using DirectoryTreePtr = std::unique_ptr<DirectoryTree>;

/// \brief Tree of `Artifact*` that can be converted to `DirectoryNodeBundle`.
class DirectoryTree {
  public:
    /// \brief Add `Artifact*` to tree.
    [[nodiscard]] auto AddArtifact(std::filesystem::path const& path,
                                   Artifact const* artifact) -> bool {
        auto const norm_path = path.lexically_normal();
        if (norm_path.empty() or
            not FileSystemManager::IsRelativePath(norm_path)) {
            return false;
        }
        auto it = norm_path.begin();
        return AddArtifact(&it, norm_path.end(), artifact);
    }

    /// \brief Convert tree to `DirectoryNodeBundle`.
    /// NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto ToBundle(
        std::string const& root_name,
        std::optional<BazelMsgFactory::BlobStoreFunc> const& store_blob,
        std::optional<BazelMsgFactory::InfoStoreFunc> const& store_info,
        std::filesystem::path const& parent = "") const
        -> DirectoryNodeBundle::Ptr {
        std::vector<bazel_re::FileNode> file_nodes;
        std::vector<bazel_re::DirectoryNode> dir_nodes;
        for (auto const& [name, node] : nodes_) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto const dir_bundle =
                    dir->ToBundle(name, store_blob, store_info, parent / name);
                if (not dir_bundle) {
                    return nullptr;
                }
                dir_nodes.push_back(dir_bundle->Message());
                if (store_blob) {
                    (*store_blob)(dir_bundle->MakeBlob());
                }
            }
            else {
                auto const& artifact = std::get<Artifact const*>(node);
                auto const& object_info = artifact->Info();
                if (not object_info) {
                    return nullptr;
                }
                if (IsTreeObject(object_info->type)) {
                    dir_nodes.push_back(
                        CreateDirectoryNodeFromObjectInfo(name, *object_info));
                }
                else {
                    file_nodes.push_back(
                        CreateFileNodeFromObjectInfo(name, *object_info));
                }
                if (store_info and
                    not(*store_info)(parent / name, *object_info)) {
                    return nullptr;
                }
            }
        }
        return CreateDirectoryNodeBundle(
            root_name, CreateDirectory(file_nodes, dir_nodes, {}, {}));
    }

    /// \brief Convert tree to `BazelBlob`.
    /// NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto ToBlob(
        std::optional<BazelMsgFactory::BlobStoreFunc> const& store_blob,
        std::optional<BazelMsgFactory::InfoStoreFunc> const& store_info,
        std::filesystem::path const& parent = "") const
        -> std::optional<BazelBlob> {
        GitCAS::tree_entries_t entries;
        entries.reserve(nodes_.size());
        for (auto const& [name, node] : nodes_) {
            if (std::holds_alternative<DirectoryTreePtr>(node)) {
                auto const& dir = std::get<DirectoryTreePtr>(node);
                auto blob = dir->ToBlob(store_blob, store_info, parent / name);
                if (not blob) {
                    return std::nullopt;
                }
                auto raw_id =
                    FromHexString(NativeSupport::Unprefix(blob->digest.hash()));
                if (not raw_id) {
                    return std::nullopt;
                }
                entries[std::move(*raw_id)].emplace_back(name,
                                                         ObjectType::Tree);
                if (store_blob) {
                    (*store_blob)(std::move(*blob));
                }
            }
            else {
                auto const& artifact = std::get<Artifact const*>(node);
                auto const& object_info = artifact->Info();
                if (not object_info) {
                    return std::nullopt;
                }
                auto raw_id = FromHexString(object_info->digest.hash());
                if (not raw_id) {
                    return std::nullopt;
                }
                entries[std::move(*raw_id)].emplace_back(name,
                                                         object_info->type);
                if (store_info and
                    not(*store_info)(parent / name, *object_info)) {
                    return std::nullopt;
                }
            }
        }

        if (auto git_tree = GitCAS::CreateShallowTree(entries)) {
            bazel_re::Digest digest{};
            digest.set_hash(NativeSupport::Prefix(ToHexString(git_tree->first),
                                                  /*is_tree=*/true));
            digest.set_size_bytes(
                gsl::narrow<google::protobuf::int64>(git_tree->second.size()));
            return BazelBlob{digest, std::move(git_tree->second)};
        }

        return std::nullopt;
    }

  private:
    using Node = std::variant<DirectoryTreePtr, Artifact const*>;
    std::unordered_map<std::string, Node> nodes_;

    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] auto AddArtifact(std::filesystem::path::iterator* begin,
                                   std::filesystem::path::iterator const& end,
                                   Artifact const* artifact) -> bool {
        auto segment = *((*begin)++);
        if (segment == "." or segment == "..") {  // fail on "." and ".."
            return false;
        }
        if (*begin == end) {
            return nodes_.emplace(segment, artifact).second;
        }
        auto const [it, success] =
            nodes_.emplace(segment, std::make_unique<DirectoryTree>());
        return (success or
                std::holds_alternative<DirectoryTreePtr>(it->second)) and
               std::get<DirectoryTreePtr>(it->second)
                   ->AddArtifact(begin, end, artifact);
    }
};

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
    GitCAS::tree_entries_t const& entries,
    InfoStoreFunc const& store_info) noexcept -> bool {
    try {
        for (auto const& [raw_id, es] : entries) {
            auto const hex_id = ToHexString(raw_id);
            for (auto const& entry : es) {
                if (not store_info(entry.name,
                                   Artifact::ObjectInfo{
                                       ArtifactDigest{hex_id,
                                                      /*size is unknown*/ 0,
                                                      IsTreeObject(entry.type)},
                                       entry.type})) {
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
    std::vector<DependencyGraph::NamedArtifactNodePtr> const& artifacts,
    std::optional<BlobStoreFunc> const& store_blob,
    std::optional<InfoStoreFunc> const& store_info)
    -> std::optional<bazel_re::Digest> {
    DirectoryTree build_root{};
    for (auto const& [local_path, node] : artifacts) {
        auto const* artifact = &node->Content();
        if (not build_root.AddArtifact(local_path, artifact)) {
            Logger::Log(LogLevel::Error,
                        "failed to add artifact {} ({}) to build root",
                        local_path,
                        artifact->Digest().value_or(ArtifactDigest{}).hash());
            return std::nullopt;
        }
    }

    if (Compatibility::IsCompatible()) {
        if (auto bundle = build_root.ToBundle("", store_blob, store_info)) {
            if (store_blob) {
                (*store_blob)(bundle->MakeBlob());
            }
            return bundle->Digest();
        }
    }
    else {
        if (auto blob = build_root.ToBlob(store_blob, store_info)) {
            auto digest = blob->digest;
            if (store_blob) {
                (*store_blob)(std::move(*blob));
            }
            return digest;
        }
    }
    return std::nullopt;
}

auto BazelMsgFactory::CreateDirectoryDigestFromLocalTree(
    std::filesystem::path const& root,
    FileStoreFunc const& store_file,
    DirStoreFunc const& store_dir) noexcept -> std::optional<bazel_re::Digest> {
    std::vector<bazel_re::FileNode> files{};
    std::vector<bazel_re::DirectoryNode> dirs{};

    auto dir_reader = [&files, &dirs, &root, &store_file, &store_dir](
                          auto name, auto type) {
        if (IsTreeObject(type)) {
            // create and store sub directory
            auto digest = CreateDirectoryDigestFromLocalTree(
                root / name, store_file, store_dir);
            if (not digest) {
                return false;
            }

            auto dir = CreateDirectoryNode(name.string());
            dir.set_allocated_digest(
                gsl::owner<bazel_re::Digest*>{new bazel_re::Digest{*digest}});
            dirs.emplace_back(std::move(dir));
            return true;
        }

        // create and store file
        try {
            const auto full_name = root / name;
            const bool is_executable =
                FileSystemManager::IsExecutable(full_name, true);
            if (auto digest = store_file(full_name, is_executable)) {
                auto file = CreateFileNode(name.string(), type, {});
                file.set_allocated_digest(gsl::owner<bazel_re::Digest*>{
                    new bazel_re::Digest{std::move(*digest)}});
                files.emplace_back(std::move(file));
                return true;
            }
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "storing file failed with:\n{}", ex.what());
        }
        return false;
    };

    if (FileSystemManager::ReadDirectory(root, dir_reader)) {
        auto dir = CreateDirectory(files, dirs, {}, {});
        if (auto bytes = SerializeMessage(dir)) {
            try {
                if (auto digest = store_dir(*bytes, dir)) {
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
    TreeStoreFunc const& store_tree) noexcept
    -> std::optional<bazel_re::Digest> {
    GitCAS::tree_entries_t entries{};
    auto dir_reader = [&entries, &root, &store_file, &store_tree](auto name,
                                                                  auto type) {
        if (IsTreeObject(type)) {
            // create and store sub directory
            if (auto digest = CreateGitTreeDigestFromLocalTree(
                    root / name, store_file, store_tree)) {
                if (auto raw_id = FromHexString(
                        NativeSupport::Unprefix(digest->hash()))) {
                    entries[std::move(*raw_id)].emplace_back(name.string(),
                                                             ObjectType::Tree);
                    return true;
                }
            }
            return false;
        }

        // create and store file
        try {
            const auto full_name = root / name;
            const bool is_executable =
                FileSystemManager::IsExecutable(full_name, true);
            if (auto digest = store_file(full_name, is_executable)) {
                if (auto raw_id = FromHexString(
                        NativeSupport::Unprefix(digest->hash()))) {
                    entries[std::move(*raw_id)].emplace_back(
                        name.string(),
                        is_executable ? ObjectType::Executable
                                      : ObjectType::File);
                    return true;
                }
            }
        } catch (std::exception const& ex) {
            Logger::Log(
                LogLevel::Error, "storing file failed with:\n{}", ex.what());
        }
        return false;
    };

    if (FileSystemManager::ReadDirectory(root, dir_reader)) {
        if (auto tree = GitCAS::CreateShallowTree(entries)) {
            try {
                if (auto digest = store_tree(tree->second, entries)) {
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
    std::vector<std::string> const& output_node_properties,
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
        cmd->Digest(), exec_dir, output_node_properties, do_not_cache, timeout);

    if (store_blob) {
        (*store_blob)(cmd->MakeBlob());
        (*store_blob)(action->MakeBlob());
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
    GitCAS::tree_entries_t const& entries) noexcept
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
