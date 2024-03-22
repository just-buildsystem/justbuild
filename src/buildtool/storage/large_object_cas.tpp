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

#include <cstdint>
#include <cstdlib>
#include <fstream>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/file_chunker.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"
#include "src/buildtool/storage/local_cas.hpp"

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::GetEntryPath(
    bazel_re::Digest const& digest) const noexcept
    -> std::optional<std::filesystem::path> {
    const std::string hash = NativeSupport::Unprefix(digest.hash());
    const std::filesystem::path file_path = file_store_.GetPath(hash);
    if (FileSystemManager::IsFile(file_path)) {
        return file_path;
    }

    if constexpr (kDoGlobalUplink) {
        // To promote parts of the tree properly, regular uplinking logic for
        // trees is used:
        bool uplinked =
            IsTreeObject(kType) and not Compatibility::IsCompatible()
                ? GarbageCollector::GlobalUplinkTree(digest)
                : GarbageCollector::GlobalUplinkLargeBlob(digest);
        if (uplinked and FileSystemManager::IsFile(file_path)) {
            return file_path;
        }
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::ReadEntry(
    bazel_re::Digest const& digest) const noexcept
    -> std::optional<std::vector<bazel_re::Digest>> {
    auto const file_path = GetEntryPath(digest);
    if (not file_path) {
        return std::nullopt;
    }

    std::vector<bazel_re::Digest> parts;
    try {
        std::ifstream stream(*file_path);
        nlohmann::json j = nlohmann::json::parse(stream);
        const size_t size = j.at("size").template get<size_t>();
        parts.reserve(size);

        auto const& j_parts = j.at("parts");
        for (size_t i = 0; i < size; ++i) {
            bazel_re::Digest& d = parts.emplace_back();
            d.set_hash(j_parts.at(i).at("hash").template get<std::string>());
            d.set_size_bytes(j_parts.at(i).at("size").template get<int64_t>());
        }
    } catch (...) {
        return std::nullopt;
    }
    return parts;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::WriteEntry(
    bazel_re::Digest const& digest,
    std::vector<bazel_re::Digest> const& parts) const noexcept -> bool {
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
        j["size"] = parts.size();
        auto& j_parts = j["parts"];
        for (auto const& part : parts) {
            auto& j = j_parts.emplace_back();
            j["hash"] = part.hash();
            j["size"] = part.size_bytes();
        }
    } catch (...) {
        return false;
    }

    const auto hash = NativeSupport::Unprefix(digest.hash());
    return file_store_.AddFromBytes(hash, j.dump());
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::Split(
    bazel_re::Digest const& digest) const noexcept
    -> std::variant<LargeObjectError, std::vector<bazel_re::Digest>> {
    if (auto large_entry = ReadEntry(digest)) {
        return std::move(*large_entry);
    }

    // Get path to the file:
    auto file_path = IsTreeObject(kType)
                         ? local_cas_.TreePath(digest)
                         : local_cas_.BlobPath(digest, /*is_executable=*/false);
    if (not file_path) {
        return LargeObjectError{
            LargeObjectErrorCode::FileNotFound,
            fmt::format("could not find {}", digest.hash())};
    }

    // Split file into chunks:
    FileChunker chunker{*file_path};
    if (not chunker.IsOpen()) {
        return LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not split {}", digest.hash())};
    }

    std::vector<bazel_re::Digest> parts;
    try {
        while (auto chunk = chunker.NextChunk()) {
            auto part = local_cas_.StoreBlob(*chunk, /*is_executable=*/false);
            if (not part) {
                return LargeObjectError{LargeObjectErrorCode::Internal,
                                        "could not store a part."};
            }
            parts.push_back(std::move(*part));
        }
    } catch (...) {
        return LargeObjectError{LargeObjectErrorCode::Internal,
                                "an unknown error occured."};
    }
    if (not chunker.Finished()) {
        return LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not split {}", digest.hash())};
    }

    std::ignore = WriteEntry(digest, parts);
    return parts;
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::TrySplice(
    bazel_re::Digest const& digest) const noexcept
    -> std::variant<LargeObjectError, LargeObject> {
    auto parts = ReadEntry(digest);
    if (not parts) {
        return LargeObjectError{
            LargeObjectErrorCode::FileNotFound,
            fmt::format("could not find large entry for {}", digest.hash())};
    }
    return Splice(digest, *parts);
}

template <bool kDoGlobalUplink, ObjectType kType>
auto LargeObjectCAS<kDoGlobalUplink, kType>::Splice(
    bazel_re::Digest const& digest,
    std::vector<bazel_re::Digest> const& parts) const noexcept
    -> std::variant<LargeObjectError, LargeObject> {
    // Create temporary space for splicing:
    LargeObject large_object;
    if (not large_object.IsValid()) {
        return LargeObjectError{
            LargeObjectErrorCode::Internal,
            fmt::format("could not create a temporary space for {}",
                        digest.hash())};
    }

    // Splice the object from parts
    try {
        std::ofstream stream(large_object.GetPath());
        for (auto const& part : parts) {
            auto part_path = local_cas_.BlobPath(part, /*is_executable=*/false);
            if (not part_path) {
                return LargeObjectError{
                    LargeObjectErrorCode::FileNotFound,
                    fmt::format("could not find the part {}", part.hash())};
            }

            auto part_content = FileSystemManager::ReadFile(*part_path);
            if (not part_content) {
                return LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not read the part content {}",
                                part.hash())};
            }

            if (stream.good()) {
                stream << *part_content;
            }
            else {
                return LargeObjectError{
                    LargeObjectErrorCode::Internal,
                    fmt::format("could not splice {}", digest.hash())};
            }
        }
        stream.close();
    } catch (...) {
        return LargeObjectError{LargeObjectErrorCode::Internal,
                                "an unknown error occured"};
    }
    return large_object;
}

template <bool kDoGlobalUplink, ObjectType kType>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LargeObjectCAS<kDoGlobalUplink, kType>::
    LocalUplink(LocalCAS<false> const& latest,
                LargeObjectCAS<false, kType> const& latest_large,
                bazel_re::Digest const& digest) const noexcept -> bool {
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
        static constexpr bool is_executable = false;
        static constexpr bool skip_sync = true;
        if (not local_cas_.LocalUplinkBlob(
                latest, part, is_executable, skip_sync)) {
            return false;
        }
    }

    auto path = GetEntryPath(digest);
    if (not path) {
        return false;
    }

    const auto hash = NativeSupport::Unprefix(digest.hash());
    return latest_large.file_store_.AddFromFile(hash, *path, /*is_owner=*/true);
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_TPP
