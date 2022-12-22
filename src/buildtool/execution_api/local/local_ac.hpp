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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/garbage_collector.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

class LocalAC {
  public:
    explicit LocalAC(gsl::not_null<LocalCAS<ObjectType::File>*> cas) noexcept
        : cas_{std::move(cas)} {};

    LocalAC(LocalAC const&) = delete;
    LocalAC(LocalAC&&) = delete;
    auto operator=(LocalAC const&) -> LocalAC& = delete;
    auto operator=(LocalAC&&) -> LocalAC& = delete;
    ~LocalAC() noexcept = default;

    [[nodiscard]] auto StoreResult(
        bazel_re::Digest const& action_id,
        bazel_re::ActionResult const& result) const noexcept -> bool {
        auto bytes = result.SerializeAsString();
        auto digest = cas_->StoreBlobFromBytes(bytes);
        return (digest and file_store_.AddFromBytes(
                               NativeSupport::Unprefix(action_id.hash()),
                               digest->SerializeAsString()));
    }

    [[nodiscard]] auto CachedResult(bazel_re::Digest const& action_id)
        const noexcept -> std::optional<bazel_re::ActionResult> {
        auto id = NativeSupport::Unprefix(action_id.hash());
        auto entry_path = file_store_.GetPath(id);
        // Try to find action-cache entry in CAS generations and uplink if
        // required.
        if (not GarbageCollector::FindAndUplinkActionCacheEntry(id)) {
            logger_.Emit(LogLevel::Debug,
                         "Cache miss, entry not found {}",
                         entry_path.string());
            return std::nullopt;
        }
        auto const entry =
            FileSystemManager::ReadFile(entry_path, ObjectType::File);
        bazel_re::Digest digest{};
        if (not digest.ParseFromString(*entry)) {
            logger_.Emit(LogLevel::Warning,
                         "Parsing cache entry failed for action {}",
                         id);
            return std::nullopt;
        }
        auto src_path = cas_->BlobPath(digest);
        bazel_re::ActionResult result{};
        if (src_path) {
            auto const bytes = FileSystemManager::ReadFile(*src_path);
            if (bytes.has_value() and result.ParseFromString(*bytes)) {
                return result;
            }
        }
        logger_.Emit(LogLevel::Warning,
                     "Parsing action result failed for action {}",
                     id);
        return std::nullopt;
    }

  private:
    // The action cache stores the results of failed actions. For those to be
    // overwritable by subsequent runs we need to choose the store mode "last
    // wins" for the underlying file storage.
    static constexpr auto kStoreMode = StoreMode::LastWins;

    Logger logger_{"LocalAC"};
    gsl::not_null<LocalCAS<ObjectType::File>*> cas_;
    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_{LocalExecutionConfig::ActionCacheDir(0)};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP
