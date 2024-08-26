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

#include <memory>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/local_cas.hpp"
#include "src/buildtool/storage/uplinker.hpp"
#include "src/utils/cpp/expected.hpp"

// forward declarations
namespace build::bazel::remote::execution::v2 {
class Digest;
class ActionResult;
}  // namespace build::bazel::remote::execution::v2
namespace bazel_re = build::bazel::remote::execution::v2;

/// \brief The action cache for storing action results.
/// Supports global uplinking across all generations. The uplink is
/// automatically performed for every entry that is read and already exists in
/// an older generation.
/// \tparam kDoGlobalUplink     Enable global uplinking.
template <bool kDoGlobalUplink>
class LocalAC {
  public:
    /// Local AC generation used by GC without global uplink.
    using LocalGenerationAC = LocalAC</*kDoGlobalUplink=*/false>;

    explicit LocalAC(gsl::not_null<LocalCAS<kDoGlobalUplink> const*> const& cas,
                     GenerationConfig const& config,
                     gsl::not_null<Uplinker<kDoGlobalUplink> const*> const&
                         uplinker) noexcept
        : cas_{*cas}, file_store_{config.action_cache}, uplinker_{*uplinker} {}

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
    LocalCAS<kDoGlobalUplink> const& cas_;
    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_;
    Uplinker<kDoGlobalUplink> const& uplinker_;

    /// \brief Add an entry to the ActionCache.
    /// \param action_id The id of the action that produced the result.
    /// \param cas_key The key pointing at an ActionResult in the LocalCAS.
    /// \return True if an entry was successfully added to the storage.
    [[nodiscard]] auto WriteActionKey(
        ArtifactDigest const& action_id,
        ArtifactDigest const& cas_key) const noexcept -> bool;

    /// \brief Get the key pointing at an ActionResult in the LocalCAS.
    /// \param action_id The id of the action that produced the result.
    /// \return The key of an Action pointing at an ActionResult in the LocalCAS
    /// on success or an error message on failure.
    [[nodiscard]] auto ReadActionKey(bazel_re::Digest const& action_id)
        const noexcept -> expected<bazel_re::Digest, std::string>;

    /// \brief Add an action to the LocalCAS.
    /// \param action The action result to store.
    /// \return The key pointing at an ActionResult present in the LocalCAS on
    /// success or std::nullopt on failure.
    [[nodiscard]] auto WriteAction(bazel_re::ActionResult const& action)
        const noexcept -> std::optional<bazel_re::Digest>;

    /// \brief Get the action specified by a key from the LocalCAS.
    /// \param cas_key The key pointing at an ActionResult present in the
    /// LocalCAS.
    /// \return The ActionResult corresponding to a cas_key on success
    /// or std::nullopt on failure.
    [[nodiscard]] auto ReadAction(bazel_re::Digest const& cas_key)
        const noexcept -> std::optional<bazel_re::ActionResult>;
};

#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/storage/local_ac.tpp"
#endif

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_LOCAL_AC_HPP
