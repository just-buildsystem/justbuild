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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_STORAGE_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_STORAGE_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/local_ac.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/buildtool/storage/target_cache.hpp"

/// \brief The local storage for accessing CAS and caches.
/// Maintains an instance of LocalCAS, LocalAC, TargetCache. Supports global
/// uplinking across all generations using the garbage collector. The uplink is
/// automatically performed by the affected storage instance (CAS, action cache,
/// target cache).
/// \tparam kDoGlobalUplink     Enable global uplinking via garbage collector.
template <bool kDoGlobalUplink>
class LocalStorage {
  public:
    explicit LocalStorage(std::filesystem::path const& storage_path)
        : cas_{std::make_shared<LocalCAS<kDoGlobalUplink>>(storage_path /
                                                           "cas")},
          ac_{cas_, storage_path / "ac"},
          tc_{cas_, storage_path / "tc"} {}

    /// \brief Get the CAS instance.
    [[nodiscard]] auto CAS() const noexcept
        -> LocalCAS<kDoGlobalUplink> const& {
        return *cas_;
    }

    /// \brief Get the action cache instance.
    [[nodiscard]] auto ActionCache() const noexcept
        -> LocalAC<kDoGlobalUplink> const& {
        return ac_;
    }

    /// \brief Get the target cache instance.
    [[nodiscard]] auto TargetCache() const noexcept
        -> TargetCache<kDoGlobalUplink> const& {
        return tc_;
    }

  private:
    gsl::not_null<std::shared_ptr<LocalCAS<kDoGlobalUplink>>> cas_;
    LocalAC<kDoGlobalUplink> ac_;
    ::TargetCache<kDoGlobalUplink> tc_;
};

#ifdef BOOTSTRAP_BUILD_TOOL
// disable global uplinking (via garbage collector) for global storage singleton
constexpr auto kDefaultDoGlobalUplink = false;
#else
constexpr auto kDefaultDoGlobalUplink = true;
#endif

/// \brief Generation type, local storage without global uplinking.
using Generation = LocalStorage</*kDoGlobalUplink=*/false>;

/// \brief Global storage singleton class, valid throughout the entire tool.
/// Configured via \ref StorageConfig.
class Storage : public LocalStorage<kDefaultDoGlobalUplink> {
  public:
    /// \brief Get the global storage instance.
    /// Build root is read from \ref
    /// StorageConfig::Instance().BuildRoot().
    /// \returns The global storage singleton instance.
    [[nodiscard]] static auto Instance() noexcept -> Storage const& {
        return GetStorage();
    }

    /// \brief Get specific storage generation.
    /// Number of generations is read from \ref
    /// StorageConfig::Instance().NumGenerations().
    /// \param index    the generation index (0 is latest).
    /// \returns The specific storage generation.
    [[nodiscard]] static auto Generation(std::size_t index) noexcept
        -> ::Generation const& {
        return GetGenerations()[index];
    }

    /// \brief Reinitialize storage instance and generations.
    /// Use if global \ref StorageConfig was changed. Not thread-safe!
    static void Reinitialize() noexcept {
        GetStorage() = CreateStorage();
        GetGenerations() = CreateGenerations();
    }

  private:
    using LocalStorage<kDefaultDoGlobalUplink>::LocalStorage;

    [[nodiscard]] static auto CreateStorage() noexcept -> Storage {
        return Storage{StorageConfig::Instance().GenerationCacheDir(0)};
    }

    [[nodiscard]] static auto CreateGenerations() noexcept
        -> std::vector<::Generation> {
        auto count = StorageConfig::Instance().NumGenerations();
        std::vector<::Generation> generations{};
        generations.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            generations.emplace_back(
                StorageConfig::Instance().GenerationCacheDir(i));
        }
        return generations;
    }

    [[nodiscard]] static auto GetStorage() noexcept -> Storage& {
        static auto instance = CreateStorage();
        return instance;
    }

    [[nodiscard]] static auto GetGenerations() noexcept
        -> std::vector<::Generation>& {
        static auto generations = CreateGenerations();
        return generations;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_STORAGE_HPP
