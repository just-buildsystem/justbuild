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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_HPP

#include <filesystem>
#include <optional>
#include <unordered_set>
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_cas.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"

/// \brief The local (logical) CAS for storing blobs and trees.
/// Blobs can be stored/queried as executable or non-executable. Trees might be
/// treated differently depending on the compatibility mode. Supports global
/// uplinking across all generations using the garbage collector. The uplink
/// is automatically performed for every entry that is read and every entry that
/// is stored and already exists in an older generation.
/// \tparam kDoGlobalUplink     Enable global uplinking via garbage collector.
template <bool kDoGlobalUplink>
class LocalCAS {
  public:
    /// Local CAS generation used by GC without global uplink.
    using LocalGenerationCAS = LocalCAS</*kDoGlobalUplink=*/false>;

    /// \brief Create local CAS with base path.
    /// Note that the base path is concatenated by a single character
    /// 'f'/'x'/'t' for each internally used physical CAS.
    /// \param base     The base path for the CAS.
    explicit LocalCAS(std::filesystem::path const& base)
        : cas_file_{base.string() + 'f', Uplinker<ObjectType::File>()},
          cas_exec_{base.string() + 'x', Uplinker<ObjectType::Executable>()},
          cas_tree_{base.string() + (Compatibility::IsCompatible() ? 'f' : 't'),
                    Uplinker<ObjectType::Tree>()},
          cas_file_large_{*this, base.string() + "-large-f"},
          cas_tree_large_{*this,
                          base.string() + "-large-" +
                              (Compatibility::IsCompatible() ? 'f' : 't')} {}

    /// \brief Obtain path to the storage root.
    /// \param type             Type of the storage to be obtained.
    /// \param large            True if a large storage is needed.
    [[nodiscard]] auto StorageRoot(ObjectType type,
                                   bool large = false) const noexcept
        -> std::filesystem::path const& {
        if (large) {
            return IsTreeObject(type) ? cas_tree_large_.StorageRoot()
                                      : cas_file_large_.StorageRoot();
        }
        switch (type) {
            case ObjectType::Tree:
                return cas_tree_.StorageRoot();
            case ObjectType::Executable:
                return cas_exec_.StorageRoot();
            default:
                return cas_file_.StorageRoot();
        }
    }

    /// \brief Store blob from file path with x-bit.
    /// \tparam kOwner          Indicates ownership for optimization (hardlink).
    /// \param file_path        The path of the file to store as blob.
    /// \param is_executable    Store blob with executable permissions.
    /// \returns Digest of the stored blob or nullopt otherwise.
    template <bool kOwner = false>
    [[nodiscard]] auto StoreBlob(std::filesystem::path const& file_path,
                                 bool is_executable) const noexcept
        -> std::optional<bazel_re::Digest> {
        return is_executable ? cas_exec_.StoreBlobFromFile(file_path, kOwner)
                             : cas_file_.StoreBlobFromFile(file_path, kOwner);
    }

    /// \brief Store blob from bytes with x-bit (default: non-executable).
    /// \param bytes            The bytes to create the blob from.
    /// \param is_executable    Store blob with executable permissions.
    /// \returns Digest of the stored blob or nullopt otherwise.
    [[nodiscard]] auto StoreBlob(std::string const& bytes,
                                 bool is_executable = false) const noexcept
        -> std::optional<bazel_re::Digest> {
        return is_executable ? cas_exec_.StoreBlobFromBytes(bytes)
                             : cas_file_.StoreBlobFromBytes(bytes);
    }

    /// \brief Store tree from file path.
    /// \tparam kOwner          Indicates ownership for optimization (hardlink).
    /// \param file_path    The path of the file to store as tree.
    /// \returns Digest of the stored tree or nullopt otherwise.
    template <bool kOwner = false>
    [[nodiscard]] auto StoreTree(std::filesystem::path const& file_path)
        const noexcept -> std::optional<bazel_re::Digest> {
        return cas_tree_.StoreBlobFromFile(file_path, kOwner);
    }

    /// \brief Store tree from bytes.
    /// \param bytes    The bytes to create the tree from.
    /// \returns Digest of the stored tree or nullopt otherwise.
    [[nodiscard]] auto StoreTree(std::string const& bytes) const noexcept
        -> std::optional<bazel_re::Digest> {
        return cas_tree_.StoreBlobFromBytes(bytes);
    }

    /// \brief Obtain blob path from digest with x-bit.
    /// Performs a synchronization if blob is only available with inverse x-bit.
    /// \param digest           Digest of the blob to lookup.
    /// \param is_executable    Lookup blob with executable permissions.
    /// \returns Path to the blob if found or nullopt otherwise.
    [[nodiscard]] auto BlobPath(bazel_re::Digest const& digest,
                                bool is_executable) const noexcept
        -> std::optional<std::filesystem::path> {
        auto const path = BlobPathNoSync(digest, is_executable);
        return path ? path : TrySyncBlob(digest, is_executable);
    }

    /// \brief Obtain blob path from digest with x-bit.
    /// Synchronization between file/executable CAS is skipped.
    /// \param digest           Digest of the blob to lookup.
    /// \param is_executable    Lookup blob with executable permissions.
    /// \returns Path to the blob if found or nullopt otherwise.
    [[nodiscard]] auto BlobPathNoSync(bazel_re::Digest const& digest,
                                      bool is_executable) const noexcept
        -> std::optional<std::filesystem::path> {
        return is_executable ? cas_exec_.BlobPath(digest)
                             : cas_file_.BlobPath(digest);
    }

    /// \brief Split a blob into chunks.
    /// \param digest           The digest of a blob to be split.
    /// \returns                Digests of the parts of the large object or an
    /// error code on failure.
    [[nodiscard]] auto SplitBlob(bazel_re::Digest const& digest) const noexcept
        -> std::variant<LargeObjectError, std::vector<bazel_re::Digest>> {
        return cas_file_large_.Split(digest);
    }

    /// \brief Splice a blob from parts.
    /// \param digest           The expected digest of the result.
    /// \param parts            The parts of the large object.
    /// \param is_executable    Splice the blob with executable permissions.
    /// \return                 The digest of the result or an error code on
    /// failure.
    [[nodiscard]] auto SpliceBlob(bazel_re::Digest const& digest,
                                  std::vector<bazel_re::Digest> const& parts,
                                  bool is_executable) const noexcept
        -> std::variant<LargeObjectError, bazel_re::Digest> {
        return is_executable ? Splice<ObjectType::Executable>(digest, parts)
                             : Splice<ObjectType::File>(digest, parts);
    }

    /// \brief Obtain tree path from digest.
    /// \param digest   Digest of the tree to lookup.
    /// \returns Path to the tree if found or nullopt otherwise.
    [[nodiscard]] auto TreePath(bazel_re::Digest const& digest) const noexcept
        -> std::optional<std::filesystem::path> {
        return cas_tree_.BlobPath(digest);
    }

    /// \brief Split a tree into chunks.
    /// \param digest           The digest of a tree to be split.
    /// \returns                Digests of the parts of the large object or an
    /// error code on failure.
    [[nodiscard]] auto SplitTree(bazel_re::Digest const& digest) const noexcept
        -> std::variant<LargeObjectError, std::vector<bazel_re::Digest>> {
        return cas_tree_large_.Split(digest);
    }

    /// \brief Splice a tree from parts.
    /// \param digest           The expected digest of the result.
    /// \param parts            The parts of the large object.
    /// \return                 The digest of the result or an error code on
    /// failure.
    [[nodiscard]] auto SpliceTree(bazel_re::Digest const& digest,
                                  std::vector<bazel_re::Digest> const& parts)
        const noexcept -> std::variant<LargeObjectError, bazel_re::Digest> {
        return Splice<ObjectType::Tree>(digest, parts);
    }

    /// \brief Traverses a tree recursively and retrieves object infos of all
    /// found blobs (leafs). Tree objects are by default not added to the result
    /// list, but converted to a path name.
    /// \param tree_digest      Digest of the tree.
    /// \param parent           Local parent path.
    /// \param include_trees    Include leaf tree objects (empty trees).
    /// \returns Pair of vectors, first containing filesystem paths, second
    /// containing object infos.
    [[nodiscard]] auto RecursivelyReadTreeLeafs(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent,
        bool include_trees = false) const noexcept
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<Artifact::ObjectInfo>>>;

    /// \brief Reads the flat content of a tree and returns object infos of all
    /// its direct entries (trees and blobs).
    /// \param tree_digest      Digest of the tree.
    /// \param parent           Local parent path.
    /// \returns Pair of vectors, first containing filesystem paths, second
    /// containing object infos.
    [[nodiscard]] auto ReadDirectTreeEntries(
        bazel_re::Digest const& tree_digest,
        std::filesystem::path const& parent) const noexcept
        -> std::optional<std::pair<std::vector<std::filesystem::path>,
                                   std::vector<Artifact::ObjectInfo>>>;

    /// \brief Check whether all parts of the tree are in the storage.
    /// \param tree_digest      Digest of the tree to be checked.
    /// \param tree_data        Content of the tree.
    /// \return                 An error on fail.
    [[nodiscard]] auto CheckTreeInvariant(bazel_re::Digest const& tree_digest,
                                          std::string const& tree_data)
        const noexcept -> std::optional<LargeObjectError>;

    /// \brief Dump artifact to file stream.
    /// Tree artifacts are pretty-printed (i.e., contents are listed) unless
    /// raw_tree is set, then the raw tree will be written to the file stream.
    /// \param info         The object info of the artifact to dump.
    /// \param stream       The file stream to dump to.
    /// \param raw_tree     Dump tree as raw blob.
    /// \returns true on success.
    [[nodiscard]] auto DumpToStream(Artifact::ObjectInfo const& info,
                                    gsl::not_null<FILE*> const& stream,
                                    bool raw_tree) const noexcept -> bool;

    /// \brief Uplink blob from this generation to latest LocalCAS generation.
    /// Performs a synchronization if requested and if blob is only available
    /// with inverse x-bit. This function is only available for instances that
    /// are used as local GC generations (i.e., disabled global uplink).
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest           The latest LocalCAS generation.
    /// \param digest           The digest of the blob to uplink.
    /// \param is_executable    Uplink blob with executable permissions.
    /// \param skip_sync        Do not sync between file/executable CAS.
    /// \param splice_result    Create the result of splicing in the latest
    /// generation.
    /// \returns True if blob was successfully uplinked.
    template <bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkBlob(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        bool is_executable,
        bool skip_sync = false,
        bool splice_result = false) const noexcept -> bool;

    /// \brief Uplink tree from this generation to latest LocalCAS generation.
    /// This function is only available for instances that are used as local GC
    /// generations (i.e., disabled global uplink). Trees are uplinked deep,
    /// including all referenced entries. Note that in compatible mode we do not
    /// have the notion of "tree" and instead trees are stored as blobs.
    /// Therefore, in compatible mode this function is only used by instances
    /// that are aware of trees, such as output directories in action results or
    /// tree artifacts from target cache.
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest   The latest LocalCAS generation.
    /// \param digest   The digest of the tree to uplink.
    /// \param splice_result    Create the result of splicing in the latest
    /// generation.
    /// \returns True if tree was successfully uplinked.
    template <bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkTree(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        bool splice_result = false) const noexcept -> bool;

    /// \brief Uplink large entry from this generation to latest LocalCAS
    /// generation. This function is only available for instances that are used
    /// as local GC generations (i.e., disabled global uplink).
    /// \tparam kType       Type of the large entry to be uplinked.
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest       The latest LocalCAS generation.
    /// \param latest_large The latest LargeObjectCAS
    /// \param digest       The digest of the large entry to uplink.
    /// \returns True if the large entry was successfully uplinked.
    template <ObjectType kType, bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkLargeObject(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest) const noexcept -> bool;

  private:
    ObjectCAS<ObjectType::File> cas_file_;
    ObjectCAS<ObjectType::Executable> cas_exec_;
    ObjectCAS<ObjectType::Tree> cas_tree_;
    LargeObjectCAS<kDoGlobalUplink, ObjectType::File> cas_file_large_;
    LargeObjectCAS<kDoGlobalUplink, ObjectType::Tree> cas_tree_large_;

    /// \brief Provides uplink via "exists callback" for physical object CAS.
    template <ObjectType kType>
    [[nodiscard]] static auto Uplinker() ->
        typename ObjectCAS<kType>::ExistsFunc {
        if constexpr (kDoGlobalUplink) {
            return [](auto digest, auto /*path*/) {
                if (not Compatibility::IsCompatible()) {
                    // in non-compatible mode, do explicit deep tree uplink
                    if constexpr (IsTreeObject(kType)) {
                        return GarbageCollector::GlobalUplinkTree(digest);
                    }
                }
                // in compatible mode, treat all trees as blobs
                return GarbageCollector::GlobalUplinkBlob(
                    digest, IsExecutableObject(kType));
            };
        }
        return ObjectCAS<kType>::kDefaultExists;
    }

    /// \brief Try to sync blob between file CAS and executable CAS.
    /// \param digest        Blob digest.
    /// \param to_executable Sync direction.
    /// \returns Path to blob in target CAS.
    [[nodiscard]] auto TrySyncBlob(bazel_re::Digest const& digest,
                                   bool to_executable) const noexcept
        -> std::optional<std::filesystem::path> {
        auto const src_blob = BlobPathNoSync(digest, not to_executable);
        if (src_blob and StoreBlob(*src_blob, to_executable)) {
            return BlobPathNoSync(digest, to_executable);
        }
        return std::nullopt;
    }

    template <bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkGitTree(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        bool splice_result = false) const noexcept -> bool;

    template <bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkBazelDirectory(
        LocalGenerationCAS const& latest,
        bazel_re::Digest const& digest,
        gsl::not_null<std::unordered_set<bazel_re::Digest>*> const& seen,
        bool splice_result = false) const noexcept -> bool;

    template <ObjectType kType, bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto TrySplice(
        bazel_re::Digest const& digest) const noexcept
        -> std::optional<LargeObject>;

    template <ObjectType kType>
    [[nodiscard]] auto Splice(bazel_re::Digest const& digest,
                              std::vector<bazel_re::Digest> const& parts)
        const noexcept -> std::variant<LargeObjectError, bazel_re::Digest>;
};

#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/storage/local_cas.tpp"
#else
template <bool kDoGlobalUplink>
auto LocalCAS<kDoGlobalUplink>::CheckTreeInvariant(
    bazel_re::Digest const& tree_digest,
    std::string const& tree_data) const noexcept
    -> std::optional<LargeObjectError> {
    return std::nullopt;
}

template <bool kDoGlobalUplink>
template <ObjectType kType>
auto LocalCAS<kDoGlobalUplink>::Splice(
    bazel_re::Digest const& digest,
    std::vector<bazel_re::Digest> const& parts) const noexcept
    -> std::variant<LargeObjectError, bazel_re::Digest> {
    return LargeObjectError{LargeObjectErrorCode::Internal, "not allowed"};
}
#endif

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_CAS_HPP
