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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_TPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_TPP

#include <tuple>    //std::ignore
#include <utility>  // std::move

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/local_ac.hpp"

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::StoreResult(
    bazel_re::Digest const& action_id,
    bazel_re::ActionResult const& result) const noexcept -> bool {
    auto const cas_key = WriteAction(result);
    return cas_key.has_value() and
           WriteActionKey(static_cast<ArtifactDigest>(action_id),
                          static_cast<ArtifactDigest>(*cas_key));
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::CachedResult(bazel_re::Digest const& action_id)
    const noexcept -> std::optional<bazel_re::ActionResult> {
    auto const cas_key = ReadActionKey(action_id);
    if (not cas_key) {
        logger_->Emit(LogLevel::Debug, cas_key.error());
        return std::nullopt;
    }
    auto result = ReadAction(*cas_key);
    if (not result) {
        logger_->Emit(LogLevel::Warning,
                      "Parsing action result failed for action {}",
                      NativeSupport::Unprefix(action_id.hash()));
        return std::nullopt;
    }
    return std::move(result);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
    requires(kIsLocalGeneration)
auto LocalAC<kDoGlobalUplink>::LocalUplinkEntry(
    LocalGenerationAC const& latest,
    bazel_re::Digest const& action_id) const noexcept -> bool {
    // Determine action cache key path in latest generation.
    auto const key_digest = NativeSupport::Unprefix(action_id.hash());
    if (FileSystemManager::IsFile(latest.file_store_.GetPath(key_digest))) {
        return true;
    }

    // Read cache key
    auto const cas_key = ReadActionKey(action_id);
    if (not cas_key) {
        return false;
    }

    // Read result (cache value)
    auto const result = ReadAction(*cas_key);
    if (not result) {
        return false;
    }

    // Uplink result content
    for (auto const& file : result->output_files()) {
        if (not cas_.LocalUplinkBlob(
                latest.cas_, file.digest(), file.is_executable())) {
            return false;
        }
    }
    for (auto const& link : result->output_file_symlinks()) {
        if (not cas_.LocalUplinkBlob(latest.cas_,
                                     ArtifactDigest::Create<ObjectType::File>(
                                         cas_.GetHashFunction(), link.target()),
                                     /*is_executable=*/false)) {
            return false;
        }
    }
    for (auto const& link : result->output_directory_symlinks()) {
        if (not cas_.LocalUplinkBlob(latest.cas_,
                                     ArtifactDigest::Create<ObjectType::File>(
                                         cas_.GetHashFunction(), link.target()),
                                     /*is_executable=*/false)) {
            return false;
        }
    }
    for (auto const& directory : result->output_directories()) {
        if (not cas_.LocalUplinkTree(latest.cas_, directory.tree_digest())) {
            return false;
        }
    }

    // Uplink result (cache value)
    if (not cas_.LocalUplinkBlob(latest.cas_,
                                 *cas_key,
                                 /*is_executable=*/false)) {
        return false;
    }

    auto const ac_entry_path = file_store_.GetPath(key_digest);
    // Uplink cache key
    return latest.file_store_.AddFromFile(key_digest,
                                          ac_entry_path,
                                          /*is_owner=*/true);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::WriteActionKey(
    ArtifactDigest const& action_id,
    ArtifactDigest const& cas_key) const noexcept -> bool {
    nlohmann::json const content = {cas_key.hash(), cas_key.size()};
    return file_store_.AddFromBytes(action_id.hash(), content.dump());
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::ReadActionKey(bazel_re::Digest const& action_id)
    const noexcept -> expected<bazel_re::Digest, std::string> {
    auto const key_path =
        file_store_.GetPath(NativeSupport::Unprefix(action_id.hash()));

    if constexpr (kDoGlobalUplink) {
        // Uplink any existing action-cache entries in storage generations
        std::ignore = uplinker_.UplinkActionCacheEntry(action_id);
    }

    auto const key_content =
        FileSystemManager::ReadFile(key_path, ObjectType::File);
    if (not key_content) {
        return unexpected{
            fmt::format("Cache miss, entry not found {}", key_path.string())};
    }

    std::optional<bazel_re::Digest> action_key;
    try {
        nlohmann::json j = nlohmann::json::parse(*key_content);
        action_key = ArtifactDigest{j[0].template get<std::string>(),
                                    j[1].template get<std::size_t>(),
                                    /*is_tree=*/false};
    } catch (...) {
        return unexpected{
            fmt::format("Parsing cache entry failed for action {}",
                        NativeSupport::Unprefix(action_id.hash()))};
    }
    return *std::move(action_key);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::WriteAction(bazel_re::ActionResult const& action)
    const noexcept -> std::optional<bazel_re::Digest> {
    return cas_.StoreBlob(action.SerializeAsString(),
                          /*is_executable=*/false);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::ReadAction(bazel_re::Digest const& cas_key)
    const noexcept -> std::optional<bazel_re::ActionResult> {
    auto const action_path = cas_.BlobPath(cas_key,
                                           /*is_executable=*/false);
    if (not action_path) {
        return std::nullopt;
    }
    auto const action_content = FileSystemManager::ReadFile(*action_path);
    if (not action_content) {
        return std::nullopt;
    }

    bazel_re::ActionResult action{};
    if (action.ParseFromString(*action_content)) {
        return std::move(action);
    }
    return std::nullopt;
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_TPP
