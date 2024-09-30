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

#ifndef INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP
#define INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP

#include <cstddef>
#include <string>
#include <utility>  // std::move

#include "src/buildtool/crypto/hash_info.hpp"
#include "src/utils/cpp/hash_combine.hpp"

// Provides getter for size with convenient non-protobuf type. Contains an
// unprefixed hex string as hash.
class ArtifactDigest final {
    friend class ArtifactDigestFactory;

  public:
    ArtifactDigest() noexcept = default;

    explicit ArtifactDigest(HashInfo hash_info, std::size_t size) noexcept
        : hash_info_{std::move(hash_info)}, size_{size} {}

    [[nodiscard]] auto hash() const& noexcept -> std::string const& {
        return hash_info_.Hash();
    }

    [[nodiscard]] auto hash() && noexcept -> std::string {
        return std::move(hash_info_).Hash();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
    [[nodiscard]] auto IsTree() const noexcept -> bool {
        return hash_info_.IsTree();
    }

    [[nodiscard]] auto operator==(ArtifactDigest const& other) const -> bool {
        return hash_info_ == other.hash_info_;
    }

  private:
    HashInfo hash_info_;
    std::size_t size_ = 0;
};

namespace std {
template <>
struct hash<ArtifactDigest> {
    [[nodiscard]] auto operator()(ArtifactDigest const& digest) const noexcept
        -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, digest.hash());
        hash_combine(&seed, digest.IsTree());
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_COMMON_ARTIFACT_DIGEST_HPP
