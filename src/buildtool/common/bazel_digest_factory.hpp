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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_DIGEST_FACTORY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_DIGEST_FACTORY_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/object_type.hpp"

class BazelDigestFactory final {
    static constexpr auto kBlobTag = "62";
    static constexpr auto kTreeTag = "74";

  public:
    /// \brief Hash content using hash function and return a valid
    /// bazel_re::Digest
    /// \tparam kType       Type of the hashing algorithm to be used
    /// \param hash_function Hash function to be used for hashing
    /// \param content      Content to be hashed
    /// \return The digest of the content
    template <ObjectType kType>
    [[nodiscard]] static auto HashDataAs(HashFunction hash_function,
                                         std::string const& content)
        -> bazel_re::Digest {
        static constexpr bool kIsTree = IsTreeObject(kType);
        auto const hash_digest = kIsTree ? hash_function.HashTreeData(content)
                                         : hash_function.HashBlobData(content);

        auto hash = hash_function.GetType() == HashFunction::Type::GitSHA1
                        ? Prefix(hash_digest.HexString(), kIsTree)
                        : hash_digest.HexString();

        bazel_re::Digest digest{};
        digest.set_hash(std::move(hash));
        digest.set_size_bytes(gsl::narrow<std::int64_t>(content.size()));
        return digest;
    }

  private:
    [[nodiscard]] static auto Prefix(std::string const& hash,
                                     bool is_tree) noexcept -> std::string {
        return (is_tree ? kTreeTag : kBlobTag) + hash;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_DIGEST_FACTORY_HPP
