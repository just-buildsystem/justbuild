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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_KEY_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_KEY_HPP

#include <utility>

#include "src/buildtool/common/artifact.hpp"

// Key for target cache. Created from target name and effective config.
class TargetCacheKey {
  public:
    explicit TargetCacheKey(Artifact::ObjectInfo id) : id_{std::move(id)} {}

    [[nodiscard]] auto Id() const& -> Artifact::ObjectInfo const& {
        return id_;
    }
    [[nodiscard]] auto Id() && -> Artifact::ObjectInfo {
        return std::move(id_);
    }
    [[nodiscard]] auto operator==(TargetCacheKey const& other) const -> bool {
        return this->Id() == other.Id();
    }

  private:
    Artifact::ObjectInfo id_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_TARGET_CACHE_KEY_HPP
