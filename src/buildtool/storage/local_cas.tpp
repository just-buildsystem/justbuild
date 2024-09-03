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
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/local_cas.hpp"

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::LocalUplinkBlob(
    LocalGenerationCAS const& latest,
    ArtifactDigest const& digest,
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
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::LocalUplinkTree(
    LocalGenerationCAS const& latest,
    ArtifactDigest const& digest,
    bool splice_result) const noexcept -> bool {
    if (Compatibility::IsCompatible()) {
        std::unordered_set<ArtifactDigest> seen{};
        return LocalUplinkBazelDirectory(latest, digest, &seen, splice_result);
    }
    return LocalUplinkGitTree(latest, digest, splice_result);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::LocalUplinkGitTree(
    LocalGenerationCAS const& latest,
    ArtifactDigest const& digest,
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
    auto check_symlinks =
        [this](std::vector<ArtifactDigest> const& ids) -> bool {
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
                                              digest.hash(),
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
        auto const entry_type = entry_vector.front().type;
        auto const digest =
            ArtifactDigest{ToHexString(raw_id), 0, IsTreeObject(entry_type)};
        if (digest.IsTree()) {
            if (not LocalUplinkGitTree(latest, digest)) {
                return false;
            }
        }
        else {
            if (not LocalUplinkBlob(
                    latest, digest, IsExecutableObject(entry_type))) {
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
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::LocalUplinkBazelDirectory(
    LocalGenerationCAS const& latest,
    ArtifactDigest const& digest,
    gsl::not_null<std::unordered_set<ArtifactDigest>*> const& seen,
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
        auto const digest = ArtifactDigestFactory::FromBazel(
            hash_function_.GetType(), file.digest());
        if (not digest) {
            return false;
        }
        if (not LocalUplinkBlob(latest, *digest, file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : dir.directories()) {
        auto const digest = ArtifactDigestFactory::FromBazel(
            hash_function_.GetType(), directory.digest());
        if (not digest) {
            return false;
        }
        if (not LocalUplinkBazelDirectory(latest, *digest, seen)) {
            return false;
        }
    }

    // Determine bazel directory path in latest generation.
    auto const dir_path_latest = latest.cas_tree_.BlobPath(digest);
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
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::LocalUplinkLargeObject(
    LocalGenerationCAS const& latest,
    ArtifactDigest const& digest) const noexcept -> bool {
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
    requires(kIsLocalGeneration)
auto LocalCAS<kDoGlobalUplink>::TrySplice(
    ArtifactDigest const& digest) const noexcept -> std::optional<LargeObject> {
    auto spliced = IsTreeObject(kType) ? cas_tree_large_.TrySplice(digest)
                                       : cas_file_large_.TrySplice(digest);
    return spliced and spliced->IsValid() ? std::optional{std::move(*spliced)}
                                          : std::nullopt;
}

template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::CheckTreeInvariant(
    ArtifactDigest const& tree_digest,
    std::string const& tree_data) const noexcept
    -> std::optional<LargeObjectError> {
    if (Compatibility::IsCompatible()) {
        return std::nullopt;
    }

    auto skip_symlinks = [](auto const& /*unused*/) { return true; };
    auto const entries = GitRepo::ReadTreeData(tree_data,
                                               tree_digest.hash(),
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
            auto const digest = ArtifactDigest(ToHexString(entry.first),
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
auto LocalCAS<kDoGlobalUplink>::Splice(ArtifactDigest const& digest,
                                       std::vector<ArtifactDigest> const& parts)
    const noexcept -> expected<ArtifactDigest, LargeObjectError> {
    static constexpr bool kIsTree = IsTreeObject(kType);
    static constexpr bool kIsExec = IsExecutableObject(kType);

    // Check file is spliced already:
    if (kIsTree ? TreePath(digest) : BlobPath(digest, kIsExec)) {
        return digest;
    }

    // Splice the result from parts:
    auto splice_result = kIsTree ? cas_tree_large_.Splice(digest, parts)
                                 : cas_file_large_.Splice(digest, parts);
    if (not splice_result) {
        return unexpected{std::move(splice_result).error()};
    }

    auto const& large_object = *splice_result;

    // Check digest consistency:
    // Using Store{Tree, Blob} to calculate the resulting hash and later
    // decide whether the result is valid is unreasonable, because these
    // methods can refer to a file that existed before. The direct hash
    // calculation is done instead.
    auto const& file_path = large_object.GetPath();
    auto spliced_digest =
        ArtifactDigestFactory::HashFileAs<kType>(hash_function_, file_path);
    if (not spliced_digest) {
        return unexpected{LargeObjectError{LargeObjectErrorCode::Internal,
                                           "could not calculate digest"}};
    }

    if (*spliced_digest != digest) {
        return unexpected{LargeObjectError{
            LargeObjectErrorCode::InvalidResult,
            fmt::format("actual result {} differs from the expected one {}",
                        spliced_digest->hash(),
                        digest.hash())}};
    }

    // Check tree invariants:
    if constexpr (kIsTree) {
        if (not Compatibility::IsCompatible()) {
            // Read tree entries:
            auto const tree_data = FileSystemManager::ReadFile(file_path);
            if (not tree_data) {
                return unexpected{LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not read tree {}", digest.hash())}};
            }
            if (auto error = CheckTreeInvariant(digest, *tree_data)) {
                return unexpected{std::move(*error)};
            }
        }
    }

    static constexpr bool kOwner = true;
    auto const stored_digest = kIsTree ? StoreTree<kOwner>(file_path)
                                       : StoreBlob<kOwner>(file_path, kIsExec);
    if (not stored_digest) {
        return unexpected{LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not splice {}", digest.hash())}};
    }
    return *std::move(stored_digest);
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_TPP
