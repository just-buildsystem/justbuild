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
#include <string>

#include "nlohmann/json.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/large_object_cas.hpp"

template <bool kDoGlobalUplink>
auto LargeObjectCAS<kDoGlobalUplink>::GetEntryPath(
    bazel_re::Digest const& digest) const noexcept
    -> std::optional<std::filesystem::path> {
    const std::string hash = NativeSupport::Unprefix(digest.hash());
    const std::filesystem::path file_path = file_store_.GetPath(hash);
    if (FileSystemManager::IsFile(file_path)) {
        return file_path;
    }
    return std::nullopt;
}

template <bool kDoGlobalUplink>
auto LargeObjectCAS<kDoGlobalUplink>::ReadEntry(bazel_re::Digest const& digest)
    const noexcept -> std::optional<std::vector<bazel_re::Digest>> {
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

template <bool kDoGlobalUplink>
auto LargeObjectCAS<kDoGlobalUplink>::WriteEntry(
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

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LARGE_OBJECT_CAS_TPP
