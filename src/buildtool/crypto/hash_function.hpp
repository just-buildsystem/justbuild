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

#include "src/buildtool/crypto/hasher.hpp"

#include <gsl/gsl>

/// \brief Hash function used for the entire buildtool.
class HashFunction {
  public:
    enum class JustHash : std::uint8_t {
        Native,     ///< SHA1 for plain hashes, and Git for blobs and trees.
        Compatible  ///< SHA256 for all hashes.
    };

    /// \brief Set globally used hash type.
    static void SetHashType(JustHash type) {
        [[maybe_unused]] auto _ = HashType(type);
    }

    /// \brief Compute a plain hash.
    [[nodiscard]] static auto ComputeHash(std::string const& data) noexcept
        -> Hasher::HashDigest {
        return ComputeTaggedHash(data);
    }

    /// \brief Compute a blob hash.
    [[nodiscard]] static auto ComputeBlobHash(std::string const& data) noexcept
        -> Hasher::HashDigest {
        static auto const kBlobTagCreator =
            [](std::string const& data) -> std::string {
            return {"blob " + std::to_string(data.size()) + '\0'};
        };
        return ComputeTaggedHash(data, kBlobTagCreator);
    }

    /// \brief Compute the blob hash of a file or std::nullopt on IO error.
    [[nodiscard]] static auto ComputeHashFile(
        const std::filesystem::path& file_path,
        bool as_tree) noexcept
        -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>>;

    /// \brief Compute a tree hash.
    [[nodiscard]] static auto ComputeTreeHash(std::string const& data) noexcept
        -> Hasher::HashDigest {
        static auto const kTreeTagCreator =
            [](std::string const& data) -> std::string {
            return {"tree " + std::to_string(data.size()) + '\0'};
        };
        return ComputeTaggedHash(data, kTreeTagCreator);
    }

    /// \brief Obtain incremental hasher for computing plain hashes.
    [[nodiscard]] static auto Hasher() noexcept -> ::Hasher {
        switch (HashType()) {
            case JustHash::Native:
                return ::Hasher{Hasher::HashType::SHA1};
            case JustHash::Compatible:
                return ::Hasher{Hasher::HashType::SHA256};
        }
        Ensures(false);  // unreachable
    }

  private:
    static constexpr auto kDefaultType = JustHash::Native;

    [[nodiscard]] static auto HashType(
        std::optional<JustHash> type = std::nullopt) -> JustHash {
        static JustHash type_{kDefaultType};
        if (type) {
            type_ = *type;
        }
        return type_;
    }

    [[nodiscard]] static auto ComputeTaggedHash(
        std::string const& data,
        std::function<std::string(std::string const&)> const& tag_creator =
            {}) noexcept -> Hasher::HashDigest {
        auto hasher = Hasher();
        if (tag_creator and HashType() == JustHash::Native) {
            hasher.Update(tag_creator(data));
        }
        hasher.Update(data);
        return std::move(hasher).Finalize();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_FUNCTION_HPP
