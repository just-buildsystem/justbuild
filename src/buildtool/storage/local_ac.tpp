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
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/local_ac.hpp"

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::StoreResult(
    ArtifactDigest const& action_id,
    bazel_re::ActionResult const& result) const noexcept -> bool {
    auto const cas_key = WriteAction(result);
    return cas_key.has_value() and WriteActionKey(action_id, *cas_key);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::CachedResult(ArtifactDigest const& action_id)
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
                      action_id.hash());
        return std::nullopt;
    }
    return std::move(result);
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
    requires(kIsLocalGeneration)
auto LocalAC<kDoGlobalUplink>::LocalUplinkEntry(
    LocalGenerationAC const& latest,
    ArtifactDigest const& action_id) const noexcept -> bool {
    // Determine action cache key path in latest generation.
    if (FileSystemManager::IsFile(
            latest.file_store_.GetPath(action_id.hash()))) {
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
        ArtifactDigest const a_digest{file.digest()};
        if (not cas_.LocalUplinkBlob(
                latest.cas_, a_digest, file.is_executable())) {
            return false;
        }
    }
    for (auto const& link : result->output_file_symlinks()) {
        if (not cas_.LocalUplinkBlob(
                latest.cas_,
                ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                    cas_.GetHashFunction(), link.target()),
                /*is_executable=*/false)) {
            return false;
        }
    }
    for (auto const& link : result->output_directory_symlinks()) {
        if (not cas_.LocalUplinkBlob(
                latest.cas_,
                ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                    cas_.GetHashFunction(), link.target()),
                /*is_executable=*/false)) {
            return false;
        }
    }
    for (auto const& directory : result->output_directories()) {
        ArtifactDigest const a_digest{directory.tree_digest()};
        if (not cas_.LocalUplinkTree(latest.cas_, a_digest)) {
            return false;
        }
    }

    // Uplink result (cache value)
    if (not cas_.LocalUplinkBlob(latest.cas_,
                                 *cas_key,
                                 /*is_executable=*/false)) {
        return false;
    }

    auto const ac_entry_path = file_store_.GetPath(action_id.hash());
    // Uplink cache key
    return latest.file_store_.AddFromFile(action_id.hash(),
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
auto LocalAC<kDoGlobalUplink>::ReadActionKey(ArtifactDigest const& action_id)
    const noexcept -> expected<ArtifactDigest, std::string> {
    auto const key_path = file_store_.GetPath(action_id.hash());

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

    std::optional<ArtifactDigest> action_key;
    try {
        nlohmann::json j = nlohmann::json::parse(*key_content);
        action_key = ArtifactDigest{j[0].template get<std::string>(),
                                    j[1].template get<std::size_t>(),
                                    /*is_tree=*/false};
    } catch (...) {
        return unexpected{fmt::format(
            "Parsing cache entry failed for action {}", action_id.hash())};
    }
    return *std::move(action_key);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::WriteAction(bazel_re::ActionResult const& action)
    const noexcept -> std::optional<ArtifactDigest> {
    return cas_.StoreBlob(action.SerializeAsString(),
                          /*is_executable=*/false);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::ReadAction(ArtifactDigest const& cas_key)
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
