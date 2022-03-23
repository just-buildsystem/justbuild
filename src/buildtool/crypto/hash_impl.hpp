#ifndef INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_IMPL_HPP
#define INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_IMPL_HPP

#include <optional>
#include <string>

#include "src/buildtool/logging/logger.hpp"

/// \brief Interface for hash implementations
class IHashImpl {
  public:
    IHashImpl() noexcept = default;
    IHashImpl(IHashImpl const&) = default;
    IHashImpl(IHashImpl&&) = default;
    auto operator=(IHashImpl const&) -> IHashImpl& = default;
    auto operator=(IHashImpl&&) -> IHashImpl& = default;
    virtual ~IHashImpl() = default;

    /// \brief Feed data to the incremental hashing.
    [[nodiscard]] virtual auto Update(std::string const& data) noexcept
        -> bool = 0;

    /// \brief Finalize the hashing and return hash as string of raw bytes.
    [[nodiscard]] virtual auto Finalize() && noexcept
        -> std::optional<std::string> = 0;

    /// \brief Compute the hash of data and return it as string of raw bytes.
    [[nodiscard]] virtual auto Compute(std::string const& data) && noexcept
        -> std::string = 0;

    /// \brief Get length of the hash in raw bytes.
    [[nodiscard]] virtual auto DigestLength() const noexcept -> std::size_t = 0;

    static auto FatalError() noexcept -> void {
        Logger::Log(LogLevel::Error, "Failed to compute hash.");
        std::terminate();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_CRYPTO_HASH_IMPL_HPP
