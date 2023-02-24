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

#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/storage/local_ac.hpp"

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::StoreResult(
    bazel_re::Digest const& action_id,
    bazel_re::ActionResult const& result) const noexcept -> bool {
    auto bytes = result.SerializeAsString();
    auto digest = cas_->StoreBlob(bytes);
    return (digest and
            file_store_.AddFromBytes(NativeSupport::Unprefix(action_id.hash()),
                                     digest->SerializeAsString()));
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::CachedResult(bazel_re::Digest const& action_id)
    const noexcept -> std::optional<bazel_re::ActionResult> {
    auto id = NativeSupport::Unprefix(action_id.hash());
    auto entry_path = file_store_.GetPath(id);

    if constexpr (kDoGlobalUplink) {
        // Uplink any existing action-cache entries in storage generations
        [[maybe_unused]] bool found =
            GarbageCollector::GlobalUplinkActionCacheEntry(action_id);
    }

    auto const entry =
        FileSystemManager::ReadFile(entry_path, ObjectType::File);
    if (not entry) {
        logger_->Emit(LogLevel::Debug,
                      "Cache miss, entry not found {}",
                      entry_path.string());
        return std::nullopt;
    }
    bazel_re::Digest digest{};
    if (not digest.ParseFromString(*entry)) {
        logger_->Emit(
            LogLevel::Warning, "Parsing cache entry failed for action {}", id);
        return std::nullopt;
    }
    auto result = ReadResult(digest);
    if (not result) {
        logger_->Emit(LogLevel::Warning,
                      "Parsing action result failed for action {}",
                      id);
    }
    return result;
}

template <bool kDoGlobalUplink>
template <bool kIsLocalGeneration>
requires(kIsLocalGeneration) auto LocalAC<kDoGlobalUplink>::LocalUplinkEntry(
    LocalGenerationAC const& latest,
    bazel_re::Digest const& action_id) const noexcept -> bool {
    // Determine action cache key path in latest generation.
    auto key_digest = NativeSupport::Unprefix(action_id.hash());
    if (FileSystemManager::IsFile(latest.file_store_.GetPath(key_digest))) {
        return true;
    }

    // Read cache key
    auto cache_key = file_store_.GetPath(key_digest);
    auto content = FileSystemManager::ReadFile(cache_key);
    if (not content) {
        return false;
    }

    // Read result (cache value)
    bazel_re::Digest result_digest{};
    if (not result_digest.ParseFromString(*content)) {
        return false;
    }
    auto result = ReadResult(result_digest);
    if (not result) {
        return false;
    }

    // Uplink result content
    for (auto const& file : result->output_files()) {
        if (not cas_->LocalUplinkBlob(
                *latest.cas_, file.digest(), file.is_executable())) {
            return false;
        }
    }
    for (auto const& directory : result->output_directories()) {
        if (not cas_->LocalUplinkTree(*latest.cas_, directory.tree_digest())) {
            return false;
        }
    }

    // Uplink result (cache value)
    if (not cas_->LocalUplinkBlob(*latest.cas_,
                                  result_digest,
                                  /*is_executable=*/false)) {
        return false;
    }

    // Uplink cache key
    return latest.file_store_.AddFromFile(
        key_digest, cache_key, /*is_owner=*/true);
}

template <bool kDoGlobalUplink>
auto LocalAC<kDoGlobalUplink>::ReadResult(bazel_re::Digest const& digest)
    const noexcept -> std::optional<bazel_re::ActionResult> {
    bazel_re::ActionResult result{};
    if (auto src_path = cas_->BlobPath(digest, /*is_executable=*/false)) {
        auto const bytes = FileSystemManager::ReadFile(*src_path);
        if (bytes.has_value() and result.ParseFromString(*bytes)) {
            return result;
        }
    }
    return std::nullopt;
}

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_TPP
