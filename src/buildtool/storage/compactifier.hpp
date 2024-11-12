// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFIER_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFIER_HPP

#include <cstddef>

#include "src/buildtool/storage/local_cas.hpp"

class Compactifier final {
  public:
    /// \brief Remove invalid entries from the storage. An entry is valid if the
    /// file and its parent directory have a hexadecimal name of the proper
    /// size.
    /// \param cas          Storage to be inspected.
    /// \return             True if storage does not contain invalid entries.
    [[nodiscard]] static auto RemoveInvalid(LocalCAS<false> const& cas) noexcept
        -> bool;

    /// \brief Remove spliced entries from the storage.
    /// \param local_cas      Storage to be inspected.
    /// \return               True if object storages do not contain spliced
    /// entries.
    [[nodiscard]] static auto RemoveSpliced(LocalCAS<false> const& cas) noexcept
        -> bool;

    /// \brief Split and remove from the storage every entry that is larger than
    /// the compactification threshold. Results of splitting are added to the
    /// LocalCAS.
    /// \param local_cas      LocalCAS to store results of splitting.
    /// \param threshold      Compactification threshold.
    /// \return               True if the storage doesn't contain splitable
    /// entries larger than the compactification threshold afterwards.
    [[nodiscard]] static auto SplitLarge(LocalCAS<false> const& cas,
                                         size_t threshold) noexcept -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_COMPACTIFIER_HPP
