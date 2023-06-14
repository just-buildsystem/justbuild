// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_TPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_TPP

#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/storage/local_cas.hpp"

namespace detail {

template <class T_CAS>
[[nodiscard]] auto ReadDirectory(T_CAS const& cas,
                                 bazel_re::Digest const& digest) noexcept
    -> std::optional<bazel_re::Directory> {
    if (auto const path = cas.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            return BazelMsgFactory::MessageFromString<bazel_re::Directory>(
                *content);
        }
    }
    Logger::Log(LogLevel::Error,
                "Directory {} not found in CAS",
                NativeSupport::Unprefix(digest.hash()));
    return std::nullopt;
}

template <class T_CAS>
[[nodiscard]] auto ReadGitTree(T_CAS const& cas,
                               bazel_re::Digest const& digest) noexcept
    -> std::optional<GitRepo::tree_entries_t> {
    if (auto const path = cas.TreePath(digest)) {
        if (auto const content = FileSystemManager::ReadFile(*path)) {
            auto check_symlinks =
                [&cas](std::vector<bazel_re::Digest> const& ids) {
                    for (auto const& id : ids) {
                        auto link_path =
                            cas.BlobPath(id, /*is_executable=*/false);
                        if (not link_path) {
                            return false;
                        }
                        // in the local CAS we store as files
                        auto content = FileSystemManager::ReadFile(*link_path);
                        if (not content or not PathIsNonUpwards(*content)) {
                            return false;
                        }
                    }
                    return true;
                };
            return GitRepo::ReadTreeData(
                *content,
                HashFunction::ComputeTreeHash(*content).Bytes(),
                check_symlinks,
                /*is_hex_id=*/false);
        }
    }
    Logger::Log(LogLevel::Debug,
                "Tree {} not found in CAS",
                NativeSupport::Unprefix(digest.hash()));
    return std::nullopt;
}

[[nodiscard]] inline auto DumpToStream(
    gsl::not_null<FILE*> const& stream,
    std::optional<std::string> const& data) noexcept -> bool {
    if (data) {
        std::fwrite(data->data(), 1, data->size(), stream);
        return true;
    }
    return false;
}

template <class T_CAS>
[[nodiscard]] auto TreeToStream(T_CAS const& cas,
                                bazel_re::Digest const& tree_digest,
                                gsl::not_null<FILE*> const& stream) noexcept
    -> bool {
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(cas, tree_digest)) {
            return DumpToStream(stream,
                                BazelMsgFactory::DirectoryToString(*dir));
        }
    }
    else {
        if (auto entries = ReadGitTree(cas, tree_digest)) {
            return DumpToStream(stream,
                                BazelMsgFactory::GitTreeToString(*entries));
        }
    }
    return false;
}

template <class T_CAS>
[[nodiscard]] auto BlobToStream(T_CAS const& cas,
                                Artifact::ObjectInfo const& blob_info,
                                gsl::not_null<FILE*> const& stream) noexcept
    -> bool {
    constexpr std::size_t kChunkSize{512};
    auto path =
        cas.BlobPath(blob_info.digest, IsExecutableObject(blob_info.type));
    if (not path and not Compatibility::IsCompatible()) {
        // in native mode, lookup object in tree cas to dump tree as blob
        path = cas.TreePath(blob_info.digest);
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

template <class T_CAS>
auto ReadObjectInfosRecursively(
    T_CAS const& cas,
    BazelMsgFactory::InfoStoreFunc const& store_info,
    std::filesystem::path const& parent,
    bazel_re::Digest const& digest) noexcept -> bool {
    // read from CAS
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(cas, digest)) {
            return BazelMsgFactory::ReadObjectInfosFromDirectory(
                *dir, [&cas, &store_info, &parent](auto path, auto info) {
                    return IsTreeObject(info.type)
                               ? ReadObjectInfosRecursively(cas,
                                                            store_info,
                                                            parent / path,
                                                            info.digest)
                               : store_info(parent / path, info);
                });
        }
    }
    else {
        if (auto entries = ReadGitTree(cas, digest)) {
            return BazelMsgFactory::ReadObjectInfosFromGitTree(
                *entries, [&cas, &store_info, &parent](auto path, auto info) {
                    return IsTreeObject(info.type)
                               ? ReadObjectInfosRecursively(cas,
                                                            store_info,
                                                            parent / path,
                                                            info.digest)
                               : store_info(parent / path, info);
                });
        }
    }
    return false;
}

}  // namespace detail

template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::RecursivelyReadTreeLeafs(
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

    if (detail::ReadObjectInfosRecursively(
            *this, store_info, parent, tree_digest)) {
        return std::make_pair(std::move(paths), std::move(infos));
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::ReadDirectTreeEntries(
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

    if (Compatibility::IsCompatible()) {
        if (auto dir = detail::ReadDirectory(*this, tree_digest)) {
            if (not BazelMsgFactory::ReadObjectInfosFromDirectory(
                    *dir, [&store_info, &parent](auto path, auto info) {
                        return store_info(parent / path, info);
                    })) {
                return std::nullopt;
            }
        }
    }
    else {
        if (auto entries = detail::ReadGitTree(*this, tree_digest)) {
            if (not BazelMsgFactory::ReadObjectInfosFromGitTree(
                    *entries, [&store_info, &parent](auto path, auto info) {
                        return store_info(parent / path, info);
                    })) {
                return std::nullopt;
            }
        }
    }
    return std::make_pair(std::move(paths), std::move(infos));
}

template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::DumpToStream(Artifact::ObjectInfo const& info,
                                             gsl::not_null<FILE*> const& stream,
                                             bool raw_tree) const noexcept
    -> bool {
    return IsTreeObject(info.type) and not raw_tree
               ? detail::TreeToStream(*this, info.digest, stream)
               : detail::BlobToStream(*this, info, stream);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::LocalUplinkBlob(
    LocalGenerationCAS const& latest,
    bazel_re::Digest const& digest,
    bool is_executable,
    bool skip_sync) const noexcept -> bool {
    // Determine blob path in latest generation.
    auto blob_path_latest = latest.BlobPathNoSync(digest, is_executable);
    if (blob_path_latest) {
        return true;
    }

    // Determine blob path of given generation.
    auto blob_path = skip_sync ? BlobPathNoSync(digest, is_executable)
                               : BlobPath(digest, is_executable);
    if (not blob_path) {
        return false;
    }

    // Uplink blob from older generation to the latest generation.
    return blob_path_latest.has_value() or
           latest.StoreBlob</*kOwner=*/true>(*blob_path, is_executable);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::LocalUplinkTree(
    LocalGenerationCAS const& latest,
    bazel_re::Digest const& digest) const noexcept -> bool {
    if (Compatibility::IsCompatible()) {
        std::unordered_set<bazel_re::Digest> seen{};
        return LocalUplinkBazelDirectory(latest, digest, &seen);
    }
    return LocalUplinkGitTree(latest, digest);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::LocalUplinkGitTree(
    LocalGenerationCAS const& latest,
    bazel_re::Digest const& digest) const noexcept -> bool {
    // Determine tree path in latest generation.
    auto tree_path_latest = latest.cas_tree_.BlobPath(digest);
    if (tree_path_latest) {
        return true;
    }

    // Determine tree path of given generation.
    auto tree_path = cas_tree_.BlobPath(digest);
    if (not tree_path) {
        return false;
    }

    // Determine tree entries.
    auto content = FileSystemManager::ReadFile(*tree_path);
    auto id = NativeSupport::Unprefix(digest.hash());
    auto check_symlinks =
        [cas = &cas_file_](std::vector<bazel_re::Digest> const& ids) {
            for (auto const& id : ids) {
                auto link_path = cas->BlobPath(id);
                if (not link_path) {
                    return false;
                }
                // in the local CAS we store as files
                auto content = FileSystemManager::ReadFile(*link_path);
                if (not content or not PathIsNonUpwards(*content)) {
                    return false;
                }
            }
            return true;
        };
    auto tree_entries = GitRepo::ReadTreeData(*content,
                                              id,
                                              check_symlinks,
                                              /*is_hex_id=*/true);
    if (not tree_entries) {
        return false;
    }

    // Uplink tree entries.
    for (auto const& [raw_id, entry_vector] : *tree_entries) {
        // Process only first entry from 'entry_vector' since all
        // entries represent the same blob, just with different
        // names.
        auto entry = entry_vector.front();
        auto hash = ToHexString(raw_id);
        auto digest = ArtifactDigest{hash, 0, IsTreeObject(entry.type)};
        if (entry.type == ObjectType::Tree) {
            if (not LocalUplinkGitTree(latest, digest)) {
                return false;
            }
        }
        else {
            if (not LocalUplinkBlob(
                    latest, digest, IsExecutableObject(entry.type))) {
                return false;
            }
        }
    }

    // Uplink tree from older generation to the latest generation.
    return latest.cas_tree_
        .StoreBlobFromFile(*tree_path,
                           /*is_owner=*/true)
        .has_value();
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::
    LocalUplinkBazelDirectory(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        gsl::not_null<std::unordered_set<bazel_re::Digest>*> const& seen)
        const noexcept -> bool {
    // Skip already uplinked directories
    if (seen->contains(digest)) {
        return true;
    }

    // Determine bazel directory path of given generation.
    auto dir_path = cas_tree_.BlobPath(digest);
    if (not dir_path) {
        return false;
    }

    // Determine bazel directory entries.
    auto content = FileSystemManager::ReadFile(*dir_path);
    bazel_re::Directory dir{};
    if (not dir.ParseFromString(*content)) {
        return false;
    }

    // Uplink bazel directory entries.
    for (auto const& file : dir.files()) {
        if (not LocalUplinkBlob(latest, file.digest(), file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : dir.directories()) {
        if (not LocalUplinkBazelDirectory(latest, directory.digest(), seen)) {
            return false;
        }
    }

    // Determine bazel directory path in latest generation.
    auto dir_path_latest = latest.cas_tree_.BlobPath(digest);

    // Uplink bazel directory from older generation to the latest
    // generation.
    if (dir_path_latest.has_value() or
        latest.cas_tree_.StoreBlobFromFile(*dir_path,
                                           /*is_owner=*/true)) {
        try {
            seen->emplace(digest);
            return true;
        } catch (...) {
        }
    }
    return false;
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_TPP
