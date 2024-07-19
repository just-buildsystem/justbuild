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
#include <memory>

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
class LocalStorage final {
  public:
    static constexpr std::size_t kYoungest = 0U;

    using Uplinker_t = Uplinker<kDoGlobalUplink>;
    using CAS_t = LocalCAS<kDoGlobalUplink>;
    using AC_t = LocalAC<kDoGlobalUplink>;
    using TC_t = ::TargetCache<kDoGlobalUplink>;

    [[nodiscard]] static auto Create(
        gsl::not_null<StorageConfig const*> const& storage_config,
        std::size_t generation = kYoungest) -> LocalStorage<kDoGlobalUplink> {
        if constexpr (kDoGlobalUplink) {
            // It is not allowed to enable uplinking for old generations.
            EnsuresAudit(generation == kYoungest);
        }
        auto gen_config = storage_config->CreateGenerationConfig(generation);
        return LocalStorage<kDoGlobalUplink>{gen_config};
    }

    [[nodiscard]] auto GetHashFunction() const noexcept -> HashFunction {
        return cas_->GetHashFunction();
    }

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
    std::unique_ptr<Uplinker_t const> uplinker_;
    std::unique_ptr<CAS_t const> cas_;
    std::unique_ptr<AC_t const> ac_;
    std::unique_ptr<TC_t const> tc_;

    explicit LocalStorage(GenerationConfig const& config)
        : uplinker_{std::make_unique<Uplinker_t>(config.storage_config)},
          cas_{std::make_unique<CAS_t>(config, &*uplinker_)},
          ac_{std::make_unique<AC_t>(&*cas_, config, &*uplinker_)},
          tc_{std::make_unique<TC_t>(&*cas_, config, &*uplinker_)} {}
};

#ifdef BOOTSTRAP_BUILD_TOOL
// disable global uplinking
constexpr auto kDefaultDoGlobalUplink = false;
#else
constexpr auto kDefaultDoGlobalUplink = true;
#endif

/// \brief Generation type, local storage without global uplinking.
using Generation = LocalStorage</*kDoGlobalUplink=*/false>;

using Storage = LocalStorage<kDefaultDoGlobalUplink>;

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_STORAGE_HPP
