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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_ENTRY_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_ENTRY_HPP

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/expression/target_result.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/crypto/hash_function.hpp"

// Entry for target cache. Created from target, contains TargetResult.
class TargetCacheEntry {
  public:
    explicit TargetCacheEntry(HashFunction::Type hash_type, nlohmann::json desc)
        : hash_type_{hash_type}, desc_(std::move(desc)) {}

    // Create the entry from target with replacement artifacts/infos.
    // Replacement artifacts must replace all non-known artifacts by known.
    [[nodiscard]] static auto FromTarget(
        HashFunction::Type hash_type,
        AnalysedTargetPtr const& target,
        std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
            replacements) noexcept -> std::optional<TargetCacheEntry>;

    // Create a target-cache entry from a json description.
    [[nodiscard]] static auto FromJson(HashFunction::Type hash_type,
                                       nlohmann::json desc) noexcept
        -> TargetCacheEntry;

    // Obtain TargetResult from cache entry.
    [[nodiscard]] auto ToResult() const noexcept -> std::optional<TargetResult>;

    // Obtain the implied export targets
    [[nodiscard]] auto ToImplied() const noexcept -> std::set<std::string>;

    // Obtain the implied export targets hashes as a vector of ObjectInfo,
    // excluding the digest corresponding to this entry. As opposed to
    // ToImplied, it returns nullopt on exception.
    [[nodiscard]] auto ToImpliedIds(std::string const& entry_key_hash)
        const noexcept -> std::optional<std::vector<Artifact::ObjectInfo>>;

    // Obtain all artifacts from cache entry (all should be known
    // artifacts).
    [[nodiscard]] auto ToArtifacts(
        gsl::not_null<std::vector<Artifact::ObjectInfo>*> const& infos)
        const noexcept -> bool;

    [[nodiscard]] auto ToJson() const& -> nlohmann::json const& {
        return desc_;
    }
    [[nodiscard]] auto ToJson() && -> nlohmann::json {
        return std::move(desc_);
    }

  private:
    HashFunction::Type hash_type_;
    nlohmann::json desc_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_ENTRY_HPP
