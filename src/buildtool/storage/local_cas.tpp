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

#include <cstddef>
#include <utility>  // std::move

#include "fmt/core.h"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/logging/log_level.hpp"
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

[[nodiscard]] static inline auto IsDirectoryEmpty(
    bazel_re::Directory const& dir) noexcept -> bool {
    return dir.files().empty() and dir.directories().empty() and
           dir.symlinks().empty();
}

template <class T_CAS>
auto ReadObjectInfosRecursively(
    T_CAS const& cas,
    BazelMsgFactory::InfoStoreFunc const& store_info,
    std::filesystem::path const& parent,
    bazel_re::Digest const& digest,
    bool const include_trees = false) -> bool {
    // read from CAS
    if (Compatibility::IsCompatible()) {
        if (auto dir = ReadDirectory(cas, digest)) {
            if (include_trees and IsDirectoryEmpty(*dir)) {
                if (not store_info(
                        parent, {ArtifactDigest{digest}, ObjectType::Tree})) {
                    return false;
                }
            }
            return BazelMsgFactory::ReadObjectInfosFromDirectory(
                *dir,
                [&cas, &store_info, &parent, include_trees](auto path,
                                                            auto info) {
                    return IsTreeObject(info.type)
                               ? ReadObjectInfosRecursively(cas,
                                                            store_info,
                                                            parent / path,
                                                            info.digest,
                                                            include_trees)
                               : store_info(parent / path, info);
                });
        }
    }
    else {
        if (auto entries = ReadGitTree(cas, digest)) {
            if (include_trees and entries->empty()) {
                if (not store_info(
                        parent, {ArtifactDigest{digest}, ObjectType::Tree})) {
                    return false;
                }
            }
            return BazelMsgFactory::ReadObjectInfosFromGitTree(
                *entries,
                [&cas, &store_info, &parent, include_trees](auto path,
                                                            auto info) {
                    return IsTreeObject(info.type)
                               ? ReadObjectInfosRecursively(cas,
                                                            store_info,
                                                            parent / path,
                                                            info.digest,
                                                            include_trees)
                               : store_info(parent / path, info);
                });
        }
    }
    return false;
}

[[nodiscard]] static inline auto CheckDigestConsistency(
    bazel_re::Digest const& lhs,
    bazel_re::Digest const& rhs) noexcept -> bool {
    if (lhs.hash() != rhs.hash()) {
        return false;
    }
    bool const both_known = lhs.size_bytes() != 0 and rhs.size_bytes() != 0;
    if (Compatibility::IsCompatible() or both_known) {
        return lhs.size_bytes() == rhs.size_bytes();
    }
    return true;
}

}  // namespace detail

template <bool kDoGlobalUplink>
[[nodiscard]] auto LocalCAS<kDoGlobalUplink>::RecursivelyReadTreeLeafs(
    bazel_re::Digest const& tree_digest,
    std::filesystem::path const& parent,
    bool const include_trees) const noexcept
    -> std::optional<std::pair<std::vector<std::filesystem::path>,
                               std::vector<Artifact::ObjectInfo>>> {
    std::vector<std::filesystem::path> paths{};
    std::vector<Artifact::ObjectInfo> infos{};

    auto store_info = [&paths, &infos](auto path, auto info) {
        paths.emplace_back(path);
        infos.emplace_back(info);
        return true;
    };

    try {
        if (detail::ReadObjectInfosRecursively(
                *this, store_info, parent, tree_digest, include_trees)) {
            return std::make_pair(std::move(paths), std::move(infos));
        }
    } catch (...) {
        // fallthrough
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink>
[[nodiscard]] auto LocalCAS<kDoGlobalUplink>::ReadDirectTreeEntries(
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
    bool skip_sync,
    bool splice_result) const noexcept -> bool {
    // Determine blob path in latest generation.
    auto blob_path_latest = latest.BlobPathNoSync(digest, is_executable);
    if (blob_path_latest) {
        return true;
    }

    // Determine blob path of given generation.
    auto blob_path = skip_sync ? BlobPathNoSync(digest, is_executable)
                               : BlobPath(digest, is_executable);
    std::optional<LargeObject> spliced;
    if (not blob_path) {
        spliced = TrySplice<ObjectType::File>(digest);
        blob_path = spliced ? std::optional{spliced->GetPath()} : std::nullopt;
    }
    if (not blob_path) {
        return false;
    }

    if (spliced) {
        // The result of uplinking of a large object must not affect the
        // result of uplinking in general. In other case, two sequential calls
        // to BlobPath might return different results: The first call splices
        // and uplinks the object, but fails at large entry uplinking. The
        // second call finds the tree in the youngest generation and returns.
        std::ignore = LocalUplinkLargeObject<ObjectType::File>(latest, digest);
        if (not splice_result) {
            return true;
        }
    }

    // Uplink blob from older generation to the latest generation.
    if (spliced and is_executable) {
        // During multithreaded splicing, the main process can be forked
        // (inheriting open file descriptors). In this case, an executable file
        // saved using hardlinking becomes inaccessible. To prevent this,
        // executables must be stored as copies made in a child process.
        return latest.StoreBlob</*kOwner=*/false>(*blob_path, is_executable)
            .has_value();
    }
    return latest.StoreBlob</*kOwner=*/true>(*blob_path, is_executable)
        .has_value();
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::LocalUplinkTree(
    LocalGenerationCAS const& latest,
    bazel_re::Digest const& digest,
    bool splice_result) const noexcept -> bool {
    if (Compatibility::IsCompatible()) {
        std::unordered_set<bazel_re::Digest> seen{};
        return LocalUplinkBazelDirectory(latest, digest, &seen, splice_result);
    }
    return LocalUplinkGitTree(latest, digest, splice_result);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::LocalUplinkGitTree(
    LocalGenerationCAS const& latest,
    bazel_re::Digest const& digest,
    bool splice_result) const noexcept -> bool {
    // Determine tree path in latest generation.
    auto tree_path_latest = latest.cas_tree_.BlobPath(digest);
    if (tree_path_latest) {
        return true;
    }

    // Determine tree path of given generation.
    auto tree_path = cas_tree_.BlobPath(digest);
    std::optional<LargeObject> spliced;
    if (not tree_path) {
        spliced = TrySplice<ObjectType::Tree>(digest);
        tree_path = spliced ? std::optional{spliced->GetPath()} : std::nullopt;
    }
    if (not tree_path) {
        return false;
    }

    // Determine tree entries.
    auto content = FileSystemManager::ReadFile(*tree_path);
    auto id = NativeSupport::Unprefix(digest.hash());
    auto check_symlinks =
        [this](std::vector<bazel_re::Digest> const& ids) -> bool {
        for (auto const& id : ids) {
            auto link_path = cas_file_.BlobPath(id);
            std::optional<LargeObject> spliced;
            if (not link_path) {
                spliced = TrySplice<ObjectType::File>(id);
                link_path =
                    spliced ? std::optional{spliced->GetPath()} : std::nullopt;
            }
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

    if (spliced) {
        // Uplink the large entry afterwards:
        // The result of uplinking of a large object must not affect the
        // result of uplinking in general. In other case, two sequential calls
        // to TreePath might return different results: The first call splices
        // and uplinks the object, but fails at large entry uplinking. The
        // second call finds the tree in the youngest generation and returns.
        std::ignore = LocalUplinkLargeObject<ObjectType::Tree>(latest, digest);
        if (not splice_result) {
            return true;
        }
    }

    // Uplink tree from older generation to the latest generation.
    return latest.cas_tree_.StoreBlobFromFile(*tree_path, /*is owner=*/true)
        .has_value();
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::
    LocalUplinkBazelDirectory(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        gsl::not_null<std::unordered_set<bazel_re::Digest>*> const& seen,
        bool splice_result) const noexcept -> bool {
    // Skip already uplinked directories
    if (seen->contains(digest)) {
        return true;
    }

    // Determine bazel directory path of given generation.
    auto dir_path = cas_tree_.BlobPath(digest);
    std::optional<LargeObject> spliced;
    if (not dir_path) {
        spliced = TrySplice<ObjectType::Tree>(digest);
        dir_path = spliced ? std::optional{spliced->GetPath()} : std::nullopt;
    }
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

    if (spliced) {
        // Uplink the large entry afterwards:
        // The result of uplinking of a large object must not affect the
        // result of uplinking in general. In other case, two sequential
        // calls to TreePath might return different results: The first call
        // splices and uplinks the object, but fails at large entry
        // uplinking. The second call finds the tree in the youngest
        // generation and returns.
        std::ignore = LocalUplinkLargeObject<ObjectType::Tree>(latest, digest);
    }

    bool const skip_store = spliced and not splice_result;
    // Uplink bazel directory from older generation to the latest
    // generation.
    if (skip_store or dir_path_latest.has_value() or
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

template <bool kDoGlobalUplink>
template <ObjectType kType, bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::
    LocalUplinkLargeObject(LocalGenerationCAS const& latest,
                           bazel_re::Digest const& digest) const noexcept
    -> bool {
    if constexpr (IsTreeObject(kType)) {
        return cas_tree_large_.LocalUplink(
            latest, latest.cas_tree_large_, digest);
    }
    else {
        return cas_file_large_.LocalUplink(
            latest, latest.cas_file_large_, digest);
    }
}

template <bool kDoGlobalUplink>
template <ObjectType kType, bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalCAS<kDoGlobalUplink>::TrySplice(
    bazel_re::Digest const& digest) const noexcept
    -> std::optional<LargeObject> {
    auto spliced = IsTreeObject(kType) ? cas_tree_large_.TrySplice(digest)
                                       : cas_file_large_.TrySplice(digest);
    auto* large = std::get_if<LargeObject>(&spliced);
    return large and large->IsValid() ? std::optional{std::move(*large)}
                                      : std::nullopt;
}

template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::CheckTreeInvariant(
    bazel_re::Digest const& tree_digest,
    std::string const& tree_data) const noexcept
    -> std::optional<LargeObjectError> {
    if (Compatibility::IsCompatible()) {
        return std::nullopt;
    }

    auto skip_symlinks = [](auto const& /*unused*/) { return true; };
    auto const entries =
        GitRepo::ReadTreeData(tree_data,
                              NativeSupport::Unprefix(tree_digest.hash()),
                              skip_symlinks,
                              /*is_hex_id=*/true);
    if (not entries) {
        return LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not read entries of the tree {}",
                        tree_digest.hash())};
    }

    // Ensure all entries are in the storage:
    for (const auto& entry : *entries) {
        for (auto const& item : entry.second) {
            bazel_re::Digest const digest =
                ArtifactDigest(ToHexString(entry.first),
                               /*size_unknown=*/0ULL,
                               IsTreeObject(item.type));

            // To avoid splicing during search, large CASes are inspected first.
            bool const entry_exists =
                IsTreeObject(item.type)
                    ? cas_tree_large_.GetEntryPath(digest) or TreePath(digest)
                    : cas_file_large_.GetEntryPath(digest) or
                          BlobPath(digest, IsExecutableObject(item.type));

            if (not entry_exists) {
                return LargeObjectError{
                    LargeObjectErrorCode::InvalidTree,
                    fmt::format("tree invariant violated {} : missing part {}",
                                tree_digest.hash(),
                                digest.hash())};
            }
        }
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink>
template <ObjectType kType>
auto LocalCAS<kDoGlobalUplink>::Splice(
    bazel_re::Digest const& digest,
    std::vector<bazel_re::Digest> const& parts) const noexcept
    -> std::variant<LargeObjectError, bazel_re::Digest> {
    static constexpr bool kIsTree = IsTreeObject(kType);
    static constexpr bool kIsExec = IsExecutableObject(kType);

    // Check file is spliced already:
    if (kIsTree ? TreePath(digest) : BlobPath(digest, kIsExec)) {
        return digest;
    }

    // Splice the result from parts:
    std::optional<LargeObject> large_object;
    auto splice_result = kIsTree ? cas_tree_large_.Splice(digest, parts)
                                 : cas_file_large_.Splice(digest, parts);
    if (auto* result = std::get_if<LargeObject>(&splice_result)) {
        large_object = *result;
    }
    else if (auto* error = std::get_if<LargeObjectError>(&splice_result)) {
        return std::move(*error);
    }
    else {
        return LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not splice {}", digest.hash())};
    }

    // Check digest consistency:
    // Using Store{Tree, Blob} to calculate the resulting hash and later
    // decide whether the result is valid is unreasonable, because these
    // methods can refer to a file that existed before. The direct hash
    // calculation is done instead.
    auto const file_path = large_object->GetPath();
    auto spliced_digest = ObjectCAS<kType>::CreateDigest(file_path);
    if (not spliced_digest) {
        return LargeObjectError{LargeObjectErrorCode::Internal,
                                "could not calculate digest"};
    }

    if (not detail::CheckDigestConsistency(*spliced_digest, digest)) {
        return LargeObjectError{
            LargeObjectErrorCode::InvalidResult,
            fmt::format("actual result {} differs from the expected one {}",
                        spliced_digest->hash(),
                        digest.hash())};
    }

    // Check tree invariants:
    if constexpr (kIsTree) {
        if (not Compatibility::IsCompatible()) {
            // Read tree entries:
            auto const tree_data = FileSystemManager::ReadFile(file_path);
            if (not tree_data) {
                return LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not read tree {}", digest.hash())};
            }
            if (auto error = CheckTreeInvariant(digest, *tree_data)) {
                return std::move(*error);
            }
        }
    }

    static constexpr bool kOwner = true;
    auto const stored_digest = kIsTree ? StoreTree<kOwner>(file_path)
                                       : StoreBlob<kOwner>(file_path, kIsExec);
    if (stored_digest) {
        return std::move(*stored_digest);
    }
    return LargeObjectError{LargeObjectErrorCode::Internal,
                            fmt::format("could not splice {}", digest.hash())};
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_TPP
