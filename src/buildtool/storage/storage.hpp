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
#include <memory>
#include <optional>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/local_ac.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/buildtool/storage/target_cache.hpp"
#include "src/buildtool/storage/uplinker.hpp"

/// \brief The local storage for accessing CAS and caches.
/// Maintains an instance of LocalCAS, LocalAC, TargetCache. Supports global
/// uplinking across all generations. The uplink is automatically performed by
/// the affected storage instance (CAS, action cache, target cache).
/// \tparam kDoGlobalUplink     Enable global uplinking.
template <bool kDoGlobalUplink>
class LocalStorage {
  public:
    static constexpr std::size_t kYoungest = 0U;

    using Uplinker_t = Uplinker<kDoGlobalUplink>;
    using CAS_t = LocalCAS<kDoGlobalUplink>;
    using AC_t = LocalAC<kDoGlobalUplink>;
    using TC_t = ::TargetCache<kDoGlobalUplink>;

    explicit LocalStorage(GenerationConfig const& config)
        : uplinker_{std::make_shared<Uplinker_t>(config.storage_config)},
          cas_{std::make_shared<CAS_t>(config, &*uplinker_)},
          ac_{std::make_shared<AC_t>(&*cas_, config, &*uplinker_)},
          tc_{std::make_shared<TC_t>(&*cas_, config, &*uplinker_)} {}

    /// \brief Get the CAS instance.
    [[nodiscard]] auto CAS() const noexcept -> CAS_t const& { return *cas_; }

    /// \brief Get the action cache instance.
    [[nodiscard]] auto ActionCache() const noexcept -> AC_t const& {
        return *ac_;
    }

    /// \brief Get the target cache instance.
    [[nodiscard]] auto TargetCache() const noexcept -> TC_t const& {
        return *tc_;
    }

  private:
    std::shared_ptr<Uplinker_t const> uplinker_;
    std::shared_ptr<CAS_t const> cas_;
    std::shared_ptr<AC_t const> ac_;
    std::shared_ptr<TC_t const> tc_;
};

#ifdef BOOTSTRAP_BUILD_TOOL
// disable global uplinking
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

    /// \brief Reinitialize storage instance and generations.
    /// Use if global \ref StorageConfig was changed. Not thread-safe!
    static void Reinitialize() noexcept { GetStorage() = CreateStorage(); }

  private:
    using LocalStorage<kDefaultDoGlobalUplink>::LocalStorage;

    [[nodiscard]] static auto CreateStorage() noexcept -> Storage {
        auto gen_config = StorageConfig::Instance().CreateGenerationConfig(
            Storage::kYoungest);
        return Storage{gen_config};
    }

    [[nodiscard]] static auto GetStorage() noexcept -> Storage& {
        static auto instance = CreateStorage();
        return instance;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_STORAGE_HPP
