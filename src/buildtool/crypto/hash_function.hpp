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

    /// \brief Compute a plain hash.
    [[nodiscard]] auto ComputeHash(std::string const& data) const noexcept
        -> Hasher::HashDigest {
        return ComputeTaggedHash(data);
    }

    /// \brief Compute a blob hash.
    [[nodiscard]] auto ComputeBlobHash(std::string const& data) const noexcept
        -> Hasher::HashDigest {
        static auto const kBlobTagCreator =
            [](std::string const& data) -> std::string {
            return {"blob " + std::to_string(data.size()) + '\0'};
        };
        return ComputeTaggedHash(data, kBlobTagCreator);
    }

    /// \brief Compute the blob hash of a file or std::nullopt on IO error.
    [[nodiscard]] auto ComputeHashFile(const std::filesystem::path& file_path,
                                       bool as_tree) const noexcept
        -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>>;

    /// \brief Compute a tree hash.
    [[nodiscard]] auto ComputeTreeHash(std::string const& data) const noexcept
        -> Hasher::HashDigest {
        static auto const kTreeTagCreator =
            [](std::string const& data) -> std::string {
            return {"tree " + std::to_string(data.size()) + '\0'};
        };
        return ComputeTaggedHash(data, kTreeTagCreator);
    }

    /// \brief Obtain incremental hasher for computing plain hashes.
    [[nodiscard]] auto Hasher() const noexcept -> ::Hasher {
        switch (type_) {
            case JustHash::Native:
                return ::Hasher{Hasher::HashType::SHA1};
            case JustHash::Compatible:
                return ::Hasher{Hasher::HashType::SHA256};
        }
        Ensures(false);  // unreachable
    }

  private:
    JustHash const type_;

    [[nodiscard]] auto ComputeTaggedHash(
        std::string const& data,
        std::function<std::string(std::string const&)> const& tag_creator = {})
        const noexcept -> Hasher::HashDigest {
        auto hasher = Hasher();
        if (tag_creator and type_ == JustHash::Native) {
            hasher.Update(tag_creator(data));
        }
        hasher.Update(data);
        return std::move(hasher).Finalize();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_FUNCTION_HPP
