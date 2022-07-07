#include "src/buildtool/execution_api/local/local_api.hpp"

namespace {

[[nodiscard]] auto ReadDirectory(
    gsl::not_null<LocalStorage const*> const& storage,
    bazel_re::Digest const& digest) noexcept
    -> std::optional<bazel_re::Directory> {
    if (auto const path = storage->BlobPath(digest, /*is_executable=*/false)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                *content);
        }
    }
    Logger::Log(
        LogLevel::Error, "Directory {} not found in CAS", digest.hash());
    return std::nullopt;
}

[[nodiscard]] auto ReadGitTree(
    gsl::not_null<LocalStorage const*> const& storage,
    bazel_re::Digest const& digest) noexcept
    -> std::optional<GitCAS::tree_entries_t> {
    if (auto const path = storage->TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            return GitCAS::ReadTreeData(
                *content,
                HashFunction::ComputeTreeHash(*content).Bytes(),
                /*is_hex_id=*/false);
        }
    }
    Logger::Log(LogLevel::Error, "Tree {} not found in CAS", digest.hash());
    return std::nullopt;
}

[[nodiscard]] auto DumpToStream(gsl::not_null<FILE*> const& stream,
                                std::optional<std::string> const& data) noexcept
    -> bool {
    if (data) {
        std::fwrite(data->data(), 1, data->size(), stream);
        return true;
    }
    return false;
}

[[nodiscard]] auto TreeToStream(
    gsl::not_null<LocalStorage const*> const& storage,
    bazel_re::Digest const& tree_digest,
    gsl::not_null<FILE*> const& stream) noexcept -> bool {
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(storage, tree_digest)) {
            return DumpToStream(stream,
                                BazelMsgFactory::DirectoryToString(*dir));
        }
    }
    else {
        if (auto entries = ReadGitTree(storage, tree_digest)) {
            return DumpToStream(stream,
                                BazelMsgFactory::GitTreeToString(*entries));
        }
    }
    return false;
}

[[nodiscard]] auto BlobToStream(
    gsl::not_null<LocalStorage const*> const& storage,
    Artifact::ObjectInfo const& blob_info,
    gsl::not_null<FILE*> const& stream) noexcept -> bool {
    constexpr std::size_t kChunkSize{512};
    auto path =
        storage->BlobPath(blob_info.digest, IsExecutableObject(blob_info.type));
    if (not path and not Compatibility::IsCompatible()) {
        // in native mode, lookup object in tree cas to dump tree as blob
        path = storage->TreePath(blob_info.digest);
    }
    if (path) {
        std::string data(kChunkSize, '\0');
        if (gsl::owner<FILE*> in = std::fopen(path->c_str(), "rb")) {
            while (auto size = std::fread(data.data(), 1, kChunkSize, in)) {
                std::fwrite(data.data(), 1, size, stream);
            }
            std::fclose(in);
            return true;
        }
    }
    return false;
}

}  // namespace

auto LocalStorage::ReadTreeInfos(
    bazel_re::Digest const& tree_digest,
    std::filesystem::path const& parent) const noexcept
    -> std::optional<std::pair<std::vector<std::filesystem::path>,
                               std::vector<Artifact::ObjectInfo>>> {
    std::vector<std::filesystem::path> paths{};
    std::vector<Artifact::ObjectInfo> infos{};

    auto store_info = [&paths, &infos](auto path, auto info) {
        paths.emplace_back(path);
        infos.emplace_back(info);
        return true;
    };

    if (ReadObjectInfosRecursively(store_info, parent, tree_digest)) {
        return std::make_pair(std::move(paths), std::move(infos));
    }
    return std::nullopt;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto LocalStorage::ReadObjectInfosRecursively(
    BazelMsgFactory::InfoStoreFunc const& store_info,
    std::filesystem::path const& parent,
    bazel_re::Digest const& digest) const noexcept -> bool {
    // read from in-memory tree map
    if (tree_map_) {
        auto const* tree = tree_map_->GetTree(digest);
        if (tree != nullptr) {
            return std::all_of(
                tree->begin(),
                tree->end(),
                // NOLINTNEXTLINE(misc-no-recursion)
                [this, &store_info, &parent](auto const& entry) {
                    try {
                        auto const& [path, info] = entry;
                        return IsTreeObject(info->type)
                                   ? ReadObjectInfosRecursively(store_info,
                                                                parent / path,
                                                                info->digest)
                                   : store_info(parent / path, *info);
                    } catch (...) {  // satisfy clang-tidy, store_info() could
                        return false;
                    }
                });
        }
        Logger::Log(
            LogLevel::Debug, "tree {} not found in tree map", digest.hash());
    }

    // fallback read from CAS and cache it in in-memory tree map
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(this, digest)) {
            auto tree = tree_map_ ? std::make_optional(tree_map_->CreateTree())
                                  : std::nullopt;
            return BazelMsgFactory::ReadObjectInfosFromDirectory(
                       *dir,
                       [this, &store_info, &parent, &tree](auto path,
                                                           auto info) {
                           return (not tree or tree->AddInfo(path, info)) and
                                  (IsTreeObject(info.type)
                                       ? ReadObjectInfosRecursively(
                                             store_info,
                                             parent / path,
                                             info.digest)
                                       : store_info(parent / path, info));
                       }) and
                   (not tree_map_ or
                    tree_map_->AddTree(digest, std::move(*tree)));
        }
    }
    else {
        if (auto entries = ReadGitTree(this, digest)) {
            auto tree = tree_map_ ? std::make_optional(tree_map_->CreateTree())
                                  : std::nullopt;
            return BazelMsgFactory::ReadObjectInfosFromGitTree(
                       *entries,
                       [this, &store_info, &parent, &tree](auto path,
                                                           auto info) {
                           return (not tree or tree->AddInfo(path, info)) and
                                  (IsTreeObject(info.type)
                                       ? ReadObjectInfosRecursively(
                                             store_info,
                                             parent / path,
                                             info.digest)
                                       : store_info(parent / path, info));
                       }) and
                   (not tree_map_ or
                    tree_map_->AddTree(digest, std::move(*tree)));
        }
    }
    return false;
}

auto LocalStorage::DumpToStream(Artifact::ObjectInfo const& info,
                                gsl::not_null<FILE*> const& stream,
                                bool raw_tree) const noexcept -> bool {
    return IsTreeObject(info.type) and not raw_tree
               ? TreeToStream(this, info.digest, stream)
               : BlobToStream(this, info, stream);
}
