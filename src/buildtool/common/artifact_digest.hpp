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
#include <optional>
#include <string>
#include <utility>  // std::move

#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/compatibility/native_support.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/hash_combine.hpp"

// Provides getter for size with convenient non-protobuf type. Contains a
// unprefixed hex string as hash. For communication with the execution API it
// can be cast to bazel_re::Digest which is the wire format that contains
// prefixed hashes in native mode.
class ArtifactDigest final {
  public:
    ArtifactDigest() noexcept = default;

    explicit ArtifactDigest(bazel_re::Digest const& digest) noexcept
        : size_{gsl::narrow<std::size_t>(digest.size_bytes())},
          hash_{NativeSupport::Unprefix(digest.hash())},
          // Tree information is only stored in a digest in native mode and
          // false in compatible mode.
          is_tree_{NativeSupport::IsTree(digest.hash())} {}

    ArtifactDigest(std::string hash, std::size_t size, bool is_tree) noexcept
        : size_{size},
          hash_{std::move(hash)},
          // Tree information is only stored in a digest in native mode and
          // false in compatible mode.
          is_tree_{not Compatibility::IsCompatible() and is_tree} {
        ExpectsAudit(not NativeSupport::IsPrefixed(hash_));
    }

    [[nodiscard]] auto hash() const& noexcept -> std::string const& {
        return hash_;
    }

    [[nodiscard]] auto hash() && noexcept -> std::string {
        return std::move(hash_);
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
    [[nodiscard]] auto IsTree() const noexcept -> bool { return is_tree_; }

    [[nodiscard]] explicit operator bazel_re::Digest() const {
        return CreateBazelDigest(hash_, size_, is_tree_);
    }

    [[nodiscard]] auto operator==(ArtifactDigest const& other) const -> bool {
        return std::equal_to<bazel_re::Digest>{}(
            static_cast<bazel_re::Digest>(*this),
            static_cast<bazel_re::Digest>(other));
    }

    template <ObjectType kType>
    [[nodiscard]] static auto Create(HashFunction hash_function,
                                     std::string const& content) noexcept
        -> ArtifactDigest {
        if constexpr (kType == ObjectType::Tree) {
            return ArtifactDigest{
                hash_function.HashTreeData(content).HexString(),
                content.size(),
                /*is_tree=*/true};
        }
        else {
            return ArtifactDigest{
                hash_function.HashBlobData(content).HexString(),
                content.size(),
                /*is_tree=*/false};
        }
    }

    template <ObjectType kType>
    [[nodiscard]] static auto CreateFromFile(
        HashFunction hash_function,
        std::filesystem::path const& path) noexcept
        -> std::optional<ArtifactDigest> {
        static constexpr bool kIsTree = IsTreeObject(kType);
        auto const hash = kIsTree ? hash_function.HashTreeFile(path)
                                  : hash_function.HashBlobFile(path);
        if (hash) {
            return ArtifactDigest{
                hash->first.HexString(), hash->second, kIsTree};
        }
        return std::nullopt;
    }

    [[nodiscard]] auto operator<(ArtifactDigest const& other) const -> bool {
        return (hash_ < other.hash_) or
               ((hash_ == other.hash_) and (static_cast<int>(is_tree_) <
                                            static_cast<int>(other.is_tree_)));
    }

  private:
    std::size_t size_{};
    std::string hash_{};
    bool is_tree_{};

    [[nodiscard]] static auto CreateBazelDigest(std::string const& hash,
                                                std::size_t size,
                                                bool is_tree)
        -> bazel_re::Digest {
        bazel_re::Digest d;
        d.set_hash(NativeSupport::Prefix(hash, is_tree));
        d.set_size_bytes(gsl::narrow<google::protobuf::int64>(size));
        return d;
    }
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
