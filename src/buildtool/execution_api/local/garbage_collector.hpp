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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_GARBAGE_COLLECTOR_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_GARBAGE_COLLECTOR_HPP

#include <optional>
#include <string>

#include "src/utils/cpp/file_locking.hpp"

/// \brief
class GarbageCollector {
  public:
    [[nodiscard]] auto static FindAndUplinkBlob(std::string const& id,
                                                bool is_executable) noexcept
        -> bool;

    [[nodiscard]] auto static FindAndUplinkTree(std::string const& id) noexcept
        -> bool;

    [[nodiscard]] auto static FindAndUplinkActionCacheEntry(
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static FindAndUplinkTargetCacheEntry(
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static TriggerGarbageCollection() noexcept -> bool;

    [[nodiscard]] auto static SharedLock() noexcept -> std::optional<LockFile>;

    [[nodiscard]] auto static ExclusiveLock() noexcept
        -> std::optional<LockFile>;

  private:
    [[nodiscard]] auto static LockFilePath() noexcept -> std::filesystem::path;

    [[nodiscard]] auto static UplinkBlob(int index,
                                         std::string const& id,
                                         bool is_executable) noexcept -> bool;

    [[nodiscard]] auto static UplinkTree(int index,
                                         std::string const& id) noexcept
        -> bool;

    [[nodiscard]] auto static UplinkBazelTree(int index,
                                              std::string const& id) noexcept
        -> bool;

    [[nodiscard]] auto static UplinkBazelDirectory(
        int index,
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static UplinkActionCacheEntry(
        int index,
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static UplinkActionCacheEntryBlob(
        int index,
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static UplinkTargetCacheEntry(
        int index,
        std::string const& id) noexcept -> bool;

    [[nodiscard]] auto static UplinkTargetCacheEntryBlob(
        int index,
        std::string const& id) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_GARBAGE_COLLECTOR_HPP
