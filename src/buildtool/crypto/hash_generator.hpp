#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_GENERATOR_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_GENERATOR_HPP

#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "src/buildtool/crypto/hash_impl_git.hpp"
#include "src/buildtool/crypto/hash_impl_md5.hpp"
#include "src/buildtool/crypto/hash_impl_sha1.hpp"
#include "src/buildtool/crypto/hash_impl_sha256.hpp"
#include "src/utils/cpp/hex_string.hpp"

/// \brief Hash generator, supports multiple types \ref HashType.
class HashGenerator {
  public:
    /// \brief Types of hash implementations supported by generator.
    enum class HashType { MD5, SHA1, SHA256, GIT };

    /// \brief The universal hash digest.
    /// The type of hash and the digest length depends on the hash
    /// implementation used to generated this digest.
    class HashDigest {
        friend HashGenerator;

      public:
        /// \brief Get pointer to raw bytes of digest.
        /// Length can be obtained using \ref Length.
        [[nodiscard]] auto Bytes() const -> std::string const& {
            return bytes_;
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

    /// \brief Incremental hasher.
    class Hasher {
        friend HashGenerator;

      public:
        /// \brief Feed data to the hasher.
        auto Update(std::string const& data) noexcept -> bool {
            return impl_->Update(data);
        }

        /// \brief Finalize hash.
        [[nodiscard]] auto Finalize() && noexcept -> std::optional<HashDigest> {
            auto hash = std::move(*impl_).Finalize();
            if (hash) {
                return HashDigest{*hash};
            }
            return std::nullopt;
        }

      private:
        std::unique_ptr<IHashImpl> impl_;

        explicit Hasher(std::unique_ptr<IHashImpl> impl)
            : impl_{std::move(impl)} {}
    };

    /// \brief Create hash generator for specific type.
    explicit HashGenerator(HashType type)
        : type_{type}, digest_length_{create_impl()->DigestLength()} {}
    HashGenerator(HashGenerator const&) = delete;
    HashGenerator(HashGenerator&&) = delete;
    auto operator=(HashGenerator const&) -> HashGenerator& = delete;
    auto operator=(HashGenerator&&) -> HashGenerator& = delete;
    ~HashGenerator() noexcept = default;

    /// \brief Run hash function on data.
    [[nodiscard]] auto Run(std::string const& data) const noexcept
        -> HashDigest {
        auto impl = create_impl();
        return HashDigest{std::move(*impl).Compute(data)};
    }

    [[nodiscard]] auto IncrementalHasher() const noexcept -> Hasher {
        return Hasher(create_impl());
    }

    [[nodiscard]] auto DigestLength() const noexcept -> std::size_t {
        return digest_length_;
    }

  private:
    HashType type_{};
    std::size_t digest_length_{};

    /// \brief Dispatch for creating the actual implementation
    [[nodiscard]] auto create_impl() const noexcept
        -> std::unique_ptr<IHashImpl> {
        switch (type_) {
            case HashType::MD5:
                return CreateHashImplMd5();
            case HashType::SHA1:
                return CreateHashImplSha1();
            case HashType::SHA256:
                return CreateHashImplSha256();
            case HashType::GIT:
                return CreateHashImplGit();
        }
    }
};

/// \brief Hash function used for the entire buildtool
[[maybe_unused]] [[nodiscard]] static inline auto ComputeHash(
    std::string const& data) noexcept -> std::string {
    static HashGenerator gen{HashGenerator::HashType::GIT};
    return gen.Run(data).HexString();
}

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_GENERATOR_HPP
