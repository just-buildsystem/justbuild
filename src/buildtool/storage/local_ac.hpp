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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_HPP

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/local_cas.hpp"

// forward declarations
namespace build::bazel::remote::execution::v2 {
class Digest;
class ActionResult;
}  // namespace build::bazel::remote::execution::v2
namespace bazel_re = build::bazel::remote::execution::v2;

/// \brief The action cache for storing action results.
/// Supports global uplinking across all generations using the garbage
/// collector. The uplink is automatically performed for every entry that is
/// read and already exists in an older generation.
/// \tparam kDoGlobalUplink     Enable global uplinking via garbage collector.
template <bool kDoGlobalUplink>
class LocalAC {
  public:
    /// Local AC generation used by GC without global uplink.
    using LocalGenerationAC = LocalAC</*kDoGlobalUplink=*/false>;

    LocalAC(std::shared_ptr<LocalCAS<kDoGlobalUplink>> cas,
            std::filesystem::path const& store_path) noexcept
        : cas_{std::move(cas)}, file_store_{store_path} {};

    LocalAC(LocalAC const&) = default;
    LocalAC(LocalAC&&) noexcept = default;
    auto operator=(LocalAC const&) -> LocalAC& = default;
    auto operator=(LocalAC&&) noexcept -> LocalAC& = default;
    ~LocalAC() noexcept = default;

    /// \brief Store action result.
    /// \param action_id    The id of the action that produced the result.
    /// \param result       The action result to store.
    /// \returns true on success.
    [[nodiscard]] auto StoreResult(
        bazel_re::Digest const& action_id,
        bazel_re::ActionResult const& result) const noexcept -> bool;

    /// \brief Read cached action result.
    /// \param action_id    The id of the action the result was produced by.
    /// \returns The action result if found or nullopt otherwise.
    [[nodiscard]] auto CachedResult(bazel_re::Digest const& action_id)
        const noexcept -> std::optional<bazel_re::ActionResult>;

    /// \brief Uplink entry from this generation to latest LocalAC generation.
    /// This function is only available for instances that are used as local GC
    /// generations (i.e., disabled global uplink).
    /// \tparam kIsLocalGeneration  True if this instance is a local generation.
    /// \param latest       The latest LocalAC generation.
    /// \param action_id    The id of the action used as entry key.
    /// \returns True if entry was successfully uplinked.
    template <bool kIsLocalGeneration = not kDoGlobalUplink>
    requires(kIsLocalGeneration) [[nodiscard]] auto LocalUplinkEntry(
        LocalGenerationAC const& latest,
        bazel_re::Digest const& action_id) const noexcept -> bool;

  private:
    // The action cache stores the results of failed actions. For those to be
    // overwritable by subsequent runs we need to choose the store mode "last
    // wins" for the underlying file storage. Unless this is a generation
    // (disabled global uplink), then we never want to overwrite any entries.
    static constexpr auto kStoreMode =
        kDoGlobalUplink ? StoreMode::LastWins : StoreMode::FirstWins;

    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("LocalAC")};
    gsl::not_null<std::shared_ptr<LocalCAS<kDoGlobalUplink>>> cas_;
    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_;

    [[nodiscard]] auto ReadResult(bazel_re::Digest const& digest) const noexcept
        -> std::optional<bazel_re::ActionResult>;
};

#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/storage/local_ac.tpp"
#endif

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_HPP
