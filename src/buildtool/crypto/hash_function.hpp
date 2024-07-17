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

#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_FUNCTION_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_FUNCTION_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/crypto/hasher.hpp"

/// \brief Hash function used for the entire buildtool.
class HashFunction {
  public:
    enum class JustHash : std::uint8_t {
        Native,     ///< SHA1 for plain hashes, and Git for blobs and trees.
        Compatible  ///< SHA256 for all hashes.
    };

    explicit HashFunction(JustHash type) noexcept : type_{type} {
        static_assert(
            sizeof(HashFunction) <= sizeof(void*),
            "HashFunction is passed and stored by value. If the "
            "class is extended so that its size exceeds the size of a pointer, "
            "the way how HashFunction is passed and stored must be changed.");
    }

    [[nodiscard]] auto GetHashType() const noexcept -> JustHash {
        return type_;
    }

    /// \brief Compute the blob hash of a string.
    [[nodiscard]] auto ComputeBlobHash(std::string const& data) const noexcept
        -> Hasher::HashDigest;

    /// \brief Compute the tree hash of a string.
    [[nodiscard]] auto ComputeTreeHash(std::string const& data) const noexcept
        -> Hasher::HashDigest;

    /// \brief Compute the plain hash of a string.
    [[nodiscard]] auto ComputeHash(std::string const& data) const noexcept
        -> Hasher::HashDigest;

    /// \brief Compute the blob hash of a file or std::nullopt on IO error.
    [[nodiscard]] auto HashBlobFile(
        std::filesystem::path const& path) const noexcept
        -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>>;

    /// \brief Compute the tree hash of a file or std::nullopt on IO error.
    [[nodiscard]] auto HashTreeFile(
        std::filesystem::path const& path) const noexcept
        -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>>;

    /// \brief Obtain incremental hasher for computing plain hashes.
    [[nodiscard]] auto MakeHasher() const noexcept -> Hasher {
        switch (type_) {
            case JustHash::Native:
                return Hasher{Hasher::HashType::SHA1};
            case JustHash::Compatible:
                return Hasher{Hasher::HashType::SHA256};
        }
        Ensures(false);  // unreachable
    }

  private:
    JustHash const type_;

    using TagCreator = std::function<std::string(std::size_t)>;

    [[nodiscard]] auto HashTaggedLine(std::string const& data,
                                      std::optional<TagCreator> tag_creator)
        const noexcept -> Hasher::HashDigest;

    [[nodiscard]] auto HashTaggedFile(
        std::filesystem::path const& path,
        TagCreator const& tag_creator) const noexcept
        -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_FUNCTION_HPP
