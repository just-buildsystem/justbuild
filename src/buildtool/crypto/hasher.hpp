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

#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>  // std::move

#include "src/utils/cpp/hex_string.hpp"

/// \brief Incremental hasher.
class Hasher final {
  public:
    /// \brief Types of hash implementations supported by generator.
    enum class HashType : std::uint8_t { SHA1, SHA256, SHA512 };

    /// \brief The universal hash digest.
    /// The type of hash and the digest length depends on the hash
    /// implementation used to generated this digest.
    class HashDigest final {
        friend Hasher;

      public:
        /// \brief Get pointer to raw bytes of digest.
        /// Length can be obtained using \ref Length.
        [[nodiscard]] auto Bytes() const& -> std::string const& {
            return bytes_;
        }

        [[nodiscard]] auto Bytes() && -> std::string {
            return std::move(bytes_);
        }

        /// \brief Get hexadecimal string of digest.
        /// Length is twice the length of raw bytes (\ref Length).
        [[nodiscard]] auto HexString() const -> std::string {
            return ToHexString(bytes_);
        }

        /// \brief Get digest length in raw bytes.
        [[nodiscard]] auto Length() const -> std::size_t {
            return bytes_.size();
        }

      private:
        std::string bytes_{};

        explicit HashDigest(std::string bytes) : bytes_{std::move(bytes)} {}
    };

    /// \brief Interface for hash implementations
    class IHashImpl {
      public:
        static constexpr size_t kCharsPerNumber =
            std::numeric_limits<std::uint8_t>::max() /
            std::numeric_limits<signed char>::max();

        IHashImpl() noexcept = default;
        IHashImpl(IHashImpl const&) = delete;
        IHashImpl(IHashImpl&&) = default;
        auto operator=(IHashImpl const&) -> IHashImpl& = delete;
        auto operator=(IHashImpl&&) -> IHashImpl& = default;
        virtual ~IHashImpl() = default;

        /// \brief Feed data to the incremental hashing.
        [[nodiscard]] virtual auto Update(std::string const& data) noexcept
            -> bool = 0;

        /// \brief Finalize the hashing and return hash as string of raw bytes.
        [[nodiscard]] virtual auto Finalize() && noexcept
            -> std::optional<std::string> = 0;

        /// \brief Obtain length of the resulting hash string.
        [[nodiscard]] virtual auto GetHashLength() const noexcept -> size_t = 0;
    };

    [[nodiscard]] static auto Create(HashType type) noexcept
        -> std::optional<Hasher>;

    /// \brief Feed data to the hasher.
    auto Update(std::string const& data) noexcept -> bool;

    /// \brief Finalize hash.
    [[nodiscard]] auto Finalize() && noexcept -> HashDigest;

    /// \brief Obtain length of the resulting hash string.
    [[nodiscard]] auto GetHashLength() const noexcept -> std::size_t;

  private:
    std::unique_ptr<IHashImpl> impl_;

    explicit Hasher(std::unique_ptr<IHashImpl> impl) noexcept;
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP
