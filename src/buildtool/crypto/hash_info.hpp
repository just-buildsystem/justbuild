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

#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_INFO_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_INFO_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "src/buildtool/crypto/hash_function.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief A collection of data related to a specific hash. Once it is
/// constructed, it holds a valid hexadecimal (always unprefixed) hash with some
/// additional information about the method of hashing.
class HashInfo final {
  public:
    explicit HashInfo() noexcept;

    /// \brief Build HashInfo based on 'external' data that cannot be trusted. A
    /// number of validation checks is happening
    /// \param type     Type of the hash function used to create the hash
    /// \param hash     A hexadecimal hash
    /// \param is_tree  Tree or blob. Note that trees are not allowed in the
    /// compatible mode.
    /// \return Validated HashInfo on success or an error message on failure.
    [[nodiscard]] static auto Create(HashFunction::Type type,
                                     std::string hash,
                                     bool is_tree) noexcept
        -> expected<HashInfo, std::string>;

    /// \brief Hash content and build HashInfo
    /// \param hash_function Hash function to be used
    /// \param content Content to be hashed
    /// \param is_tree Tree or blob, the type of the algorithm to be used for
    /// hashing. Note that HashInfo may return another value from IsTree in
    /// compatible mode.
    [[nodiscard]] static auto HashData(HashFunction hash_function,
                                       std::string const& content,
                                       bool is_tree) noexcept -> HashInfo;

    /// \brief Hash file and build HashInfo
    /// \param hash_function Hash function to be use
    /// \param path File to be hashed
    /// \param is_tree Tree or blob, the type of the algorithm to be used for
    /// hashing. Note that HashInfo may return another value from IsTree in
    /// compatible mode.
    /// \return A combination of the hash of the file and file's size or
    /// std::nullopt on IO failure.
    [[nodiscard]] static auto HashFile(HashFunction hash_function,
                                       std::filesystem::path const& path,
                                       bool is_tree) noexcept
        -> std::optional<std::pair<HashInfo, std::uintmax_t>>;

    [[nodiscard]] inline auto Hash() const& noexcept -> std::string const& {
        return hash_;
    }

    [[nodiscard]] inline auto Hash() && -> std::string {
        return std::move(hash_);
    }

    [[nodiscard]] inline auto HashType() const noexcept -> HashFunction::Type {
        return hash_type_;
    }

    [[nodiscard]] inline auto IsTree() const noexcept -> bool {
        return is_tree_;
    }

    [[nodiscard]] auto operator==(HashInfo const& other) const noexcept -> bool;

  private:
    std::string hash_;
    HashFunction::Type hash_type_;

    /// \brief Tree or blob algorithm was used for hashing. is_tree_ can be true
    /// in the native mode only, in compatible it falls back to false during
    /// hashing via HashData/HashFile or an error occurs during validation.
    bool is_tree_;

    explicit HashInfo(std::string hash,
                      HashFunction::Type type,
                      bool is_tree) noexcept
        : hash_{std::move(hash)}, hash_type_{type}, is_tree_{is_tree} {}

    [[nodiscard]] static auto ValidateInput(HashFunction::Type type,
                                            std::string const& hash,
                                            bool is_tree) noexcept
        -> std::optional<std::string>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_INFO_HPP
