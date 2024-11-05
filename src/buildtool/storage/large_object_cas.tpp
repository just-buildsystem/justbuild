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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_TPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_TPP

// IWYU pragma: private, include "src/buildtool/storage/large_object_cas.hpp"

#include <cstddef>
#include <cstdlib>
#include <fstream>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/file_chunker.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"
#include "src/buildtool/storage/local_cas.hpp"

namespace {
inline constexpr std::size_t kHashIndex = 0;
inline constexpr std::size_t kSizeIndex = 1;
}  // namespace

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::GetEntryPath(
    ArtifactDigest const& digest) const noexcept
    -> std::optional<std::filesystem::path> {
    std::filesystem::path file_path = file_store_.GetPath(digest.hash());
    if (FileSystemManager::IsFile(file_path)) {
        return file_path;
    }

    if constexpr (kDoGlobalUplink) {
        // To promote parts of the tree properly, regular uplinking logic for
        // trees is used:
        auto const hash_type = storage_config_.hash_function.GetType();
        bool const uplinked =
            IsTreeObject(kType) and not ProtocolTraits::IsTreeAllowed(hash_type)
                ? uplinker_.UplinkTree(digest)
                : uplinker_.UplinkLargeBlob(digest);
        if (uplinked and FileSystemManager::IsFile(file_path)) {
            return file_path;
        }
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::ReadEntry(
    ArtifactDigest const& digest) const noexcept
    -> std::optional<std::vector<ArtifactDigest>> {
    auto const file_path = GetEntryPath(digest);
    if (not file_path) {
        return std::nullopt;
    }

    std::vector<ArtifactDigest> parts;
    try {
        std::ifstream stream(*file_path);
        nlohmann::json j = nlohmann::json::parse(stream);
        parts.reserve(j.size());

        auto const hash_type = local_cas_.GetHashFunction().GetType();
        for (auto const& j_part : j) {
            auto digest = ArtifactDigestFactory::Create(
                hash_type,
                j_part.at(kHashIndex).template get<std::string>(),
                j_part.at(kSizeIndex).template get<std::size_t>(),
                /*is_tree=*/false);
            if (not digest) {
                return std::nullopt;
            }

            parts.emplace_back(*std::move(digest));
        }
    } catch (...) {
        return std::nullopt;
    }
    return parts;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::WriteEntry(
    ArtifactDigest const& digest,
    std::vector<ArtifactDigest> const& parts) const noexcept -> bool {
    if (GetEntryPath(digest)) {
        return true;
    }

    // The large entry cannot refer itself or be empty.
    // Otherwise, the digest in the main CAS would be removed during GC.
    // It would bring the LargeObjectCAS to an invalid state: the
    // large entry exists, but the parts do not.
    if (parts.size() < 2) {
        return false;
    }

    nlohmann::json j;
    try {
        for (auto const& part : parts) {
            auto& j_part = j.emplace_back();
            j_part[kHashIndex] = part.hash();
            j_part[kSizeIndex] = part.size();
        }
    } catch (...) {
        return false;
    }
    return file_store_.AddFromBytes(digest.hash(), j.dump());
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::Split(ArtifactDigest const& digest)
    const noexcept -> expected<std::vector<ArtifactDigest>, LargeObjectError> {
    if (auto large_entry = ReadEntry(digest)) {
        return std::move(*large_entry);
    }

    // Get path to the file:
    std::optional<std::filesystem::path> file_path;
    if constexpr (IsTreeObject(kType)) {
        file_path = local_cas_.TreePath(digest);
    }
    else {
        // Avoid synchronization between file/executable storages:
        static constexpr bool kIsExec = IsExecutableObject(kType);
        file_path = local_cas_.BlobPathNoSync(digest, kIsExec);
        file_path = file_path ? file_path
                              : local_cas_.BlobPathNoSync(digest, not kIsExec);
    }

    if (not file_path) {
        return unexpected{
            LargeObjectError{LargeObjectErrorCode::FileNotFound,
                             fmt::format("could not find {}", digest.hash())}};
    }

    // Split file into chunks:
    FileChunker chunker{*file_path};
    if (not chunker.IsOpen()) {
        return unexpected{
            LargeObjectError{LargeObjectErrorCode::Internal,
                             fmt::format("could not split {}", digest.hash())}};
    }

    std::vector<ArtifactDigest> parts;
    try {
        while (auto chunk = chunker.NextChunk()) {
            auto part = local_cas_.StoreBlob(*chunk, /*is_executable=*/false);
            if (not part) {
                return unexpected{LargeObjectError{
                    LargeObjectErrorCode::Internal, "could not store a part."}};
            }
            parts.emplace_back(*std::move(part));
        }
    } catch (...) {
        return unexpected{LargeObjectError{LargeObjectErrorCode::Internal,
                                           "an unknown error occured."}};
    }
    if (not chunker.Finished()) {
        return unexpected{
            LargeObjectError{LargeObjectErrorCode::Internal,
                             fmt::format("could not split {}", digest.hash())}};
    }

    std::ignore = WriteEntry(digest, parts);
    return parts;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::TrySplice(
    ArtifactDigest const& digest) const noexcept
    -> expected<LargeObject, LargeObjectError> {
    auto parts = ReadEntry(digest);
    if (not parts) {
        return unexpected{LargeObjectError{
            LargeObjectErrorCode::FileNotFound,
            fmt::format("could not find large entry for {}", digest.hash())}};
    }
    return Splice(digest, *parts);
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::Splice(
    ArtifactDigest const& digest,
    std::vector<ArtifactDigest> const& parts) const noexcept
    -> expected<LargeObject, LargeObjectError> {
    // Create temporary space for splicing:
    LargeObject large_object(storage_config_);
    if (not large_object.IsValid()) {
        return unexpected{LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not create a temporary space for {}",
                        digest.hash())}};
    }

    // Splice the object from parts
    try {
        std::ofstream stream(large_object.GetPath());
        for (auto const& part : parts) {
            auto part_path = local_cas_.BlobPath(part, /*is_executable=*/false);
            if (not part_path) {
                return unexpected{LargeObjectError{
                    LargeObjectErrorCode::FileNotFound,
                    fmt::format("could not find the part {}", part.hash())}};
            }

            auto part_content = FileSystemManager::ReadFile(*part_path);
            if (not part_content) {
                return unexpected{LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not read the part content {}",
                                part.hash())}};
            }

            if (stream.good()) {
                stream << *part_content;
            }
            else {
                return unexpected{LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not splice {}", digest.hash())}};
            }
        }
        stream.close();
    } catch (...) {
        return unexpected{LargeObjectError{LargeObjectErrorCode::Internal,
                                           "an unknown error occurred"}};
    }
    return large_object;
}

template <bool kDoGlobalUplink, ObjectType kType>
template <bool kIsLocalGeneration>
    requires(kIsLocalGeneration)
auto LargeObjectCAS<kDoGlobalUplink, kType>::LocalUplink(
    LocalCAS<false> const& latest,
    LargeObjectCAS<false, kType> const& latest_large,
    ArtifactDigest const& digest) const noexcept -> bool {
    // Check the large entry in the youngest generation:
    if (latest_large.GetEntryPath(digest)) {
        return true;
    }

    // Check the large entry in the current generation:
    auto parts = ReadEntry(digest);
    if (not parts) {
        // No large entry or the object is not large
        return true;
    }

    // Promoting the parts of the large entry:
    for (auto const& part : *parts) {
        static constexpr bool kIsExecutable = false;
        static constexpr bool kSkipSync = true;
        if (not local_cas_.LocalUplinkBlob(
                latest, part, kIsExecutable, kSkipSync)) {
            return false;
        }
    }

    auto path = GetEntryPath(digest);
    if (not path) {
        return false;
    }
    return latest_large.file_store_.AddFromFile(
        digest.hash(), *path, /*is_owner=*/true);
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_TPP
