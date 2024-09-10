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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DIGEST_FACTORY_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DIGEST_FACTORY_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"

namespace build::bazel::remote::execution::v2 {
class Digest;
}
namespace bazel_re = build::bazel::remote::execution::v2;

class ArtifactDigestFactory final {
  public:
    /// \brief Create ArtifactDigest from plain hash.
    /// \param hash_type Type of the hash function that was used for creation
    /// of the hash
    /// \param hash Hexadecimal plain hash
    /// \param size Size of the content
    /// \return A valid ArtifactDigest on success or an error message if
    /// validation fails.
    [[nodiscard]] static auto Create(HashFunction::Type hash_type,
                                     std::string hash,
                                     std::size_t size,
                                     bool is_tree) noexcept
        -> expected<ArtifactDigest, std::string>;

    /// \brief Create ArtifactDigest from bazel_re::Digest
    /// \param hash_type Type of the hash function that was used for creation of
    /// the hash
    /// \param digest Digest to be converted
    /// \return A valid ArtifactDigest on success or an error message if
    /// validation fails.
    [[nodiscard]] static auto FromBazel(HashFunction::Type hash_type,
                                        bazel_re::Digest const& digest) noexcept
        -> expected<ArtifactDigest, std::string>;

    /// \brief Convert ArtifactDigest to bazel_re::Digest. Throws an exception
    /// on a narrow conversion error.
    /// \param digest Digest to be converted.
    /// \return A valid bazel_re::Digest
    [[nodiscard]] static auto ToBazel(ArtifactDigest const& digest)
        -> bazel_re::Digest;

    /// \brief Hash content using hash function and return a valid
    /// ArtifactDigest
    /// \tparam kType       Type of the hashing algorithm to be used
    /// \param hash_function Hash function to be used for hashing
    /// \param content      Content to be hashed
    /// \return The digest of the content
    template <ObjectType kType>
    [[nodiscard]] static auto HashDataAs(HashFunction hash_function,
                                         std::string const& content) noexcept
        -> ArtifactDigest {
        auto hash_info =
            HashInfo::HashData(hash_function, content, IsTreeObject(kType));
        return ArtifactDigest{std::move(hash_info), content.size()};
    }

    /// \brief Hash file using hash function and return a valid ArtifactDigest
    /// \tparam kType       Type of the hashing algorithm to be used
    /// \param hash_function Hash function to be used for hashing
    /// \param content      Content to be hashed
    /// \return The digest of the file
    template <ObjectType kType>
    [[nodiscard]] static auto HashFileAs(
        HashFunction hash_function,
        std::filesystem::path const& path) noexcept
        -> std::optional<ArtifactDigest> {
        auto hash_info =
            HashInfo::HashFile(hash_function, path, IsTreeObject(kType));
        if (not hash_info) {
            return std::nullopt;
        }
        return ArtifactDigest{std::move(hash_info->first),
                              static_cast<std::size_t>(hash_info->second)};
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DIGEST_FACTORY_HPP
