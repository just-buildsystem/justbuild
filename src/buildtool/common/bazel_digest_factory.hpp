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
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"

class BazelDigestFactory final {
    static constexpr auto kBlobTag = "62";
    static constexpr auto kTreeTag = "74";
    static constexpr std::size_t kTagLength = 2;

  public:
    /// \brief Create bazel_re::Digest from preliminarily validated data.
    /// \param hash_data    Validated hash
    /// \param size         Size of the content
    [[nodiscard]] static auto Create(HashInfo const& hash_info,
                                     std::int64_t size) noexcept
        -> bazel_re::Digest;

    /// \brief Validate bazel_re::Digest
    /// \param hash_type    Type of the hash function that was used for creation
    /// of the hash
    /// \param digest       Digest to be validated
    /// \return Validated hash on success or an error message on failure.
    [[nodiscard]] static auto ToHashInfo(
        HashFunction::Type hash_type,
        bazel_re::Digest const& digest) noexcept
        -> expected<HashInfo, std::string>;

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
        auto const hash_info =
            HashInfo::HashData(hash_function, content, IsTreeObject(kType));
        return Create(hash_info, gsl::narrow<std::int64_t>(content.size()));
    }

  private:
    [[nodiscard]] static auto Prefix(std::string const& hash,
                                     bool is_tree) noexcept -> std::string {
        return (is_tree ? kTreeTag : kBlobTag) + hash;
    }

    [[nodiscard]] static auto Unprefix(std::string const& hash) noexcept
        -> std::string {
        return hash.substr(kTagLength);
    }

    [[nodiscard]] static auto IsPrefixed(HashFunction::Type hash_type,
                                         std::string const& hash) noexcept
        -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_BAZEL_DIGEST_FACTORY_HPP
