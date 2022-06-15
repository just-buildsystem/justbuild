#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

/// \brief Incremental hasher.
class Hasher {
  public:
    /// \brief Types of hash implementations supported by generator.
    enum class HashType : std::uint8_t { SHA1, SHA256 };

    /// \brief The universal hash digest.
    /// The type of hash and the digest length depends on the hash
    /// implementation used to generated this digest.
    class HashDigest {
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

        /// \brief Compute the hash of data and return it as string of raw
        /// bytes.
        [[nodiscard]] virtual auto Compute(std::string const& data) && noexcept
            -> std::string = 0;

        static auto FatalError() noexcept -> void {
            Logger::Log(LogLevel::Error, "Failed to compute hash.");
            std::terminate();
        }
    };

    explicit Hasher(HashType type) : impl_{CreateHashImpl(type)} {}

    /// \brief Feed data to the hasher.
    auto Update(std::string const& data) noexcept -> bool {
        return impl_->Update(data);
    }

    /// \brief Finalize hash.
    [[nodiscard]] auto Finalize() && noexcept -> HashDigest {
        if (auto hash = std::move(*impl_).Finalize()) {
            return HashDigest{std::move(*hash)};
        }
        IHashImpl::FatalError();
        return HashDigest{{}};
    }

  private:
    std::unique_ptr<IHashImpl> impl_;

    [[nodiscard]] static auto CreateHashImpl(HashType type) noexcept
        -> std::unique_ptr<IHashImpl>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASHER_HPP
