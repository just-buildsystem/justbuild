#include <array>
#include <cstdint>

#include "openssl/sha.h"
#include "src/buildtool/crypto/hash_impl.hpp"

/// \brief Hash implementation for SHA-256
class HashImplSha256 final : public IHashImpl {
  public:
    HashImplSha256() { initialized_ = SHA256_Init(&ctx_) == 1; }

    auto Update(std::string const& data) noexcept -> bool final {
        return initialized_ and
               SHA256_Update(&ctx_, data.data(), data.size()) == 1;
    }

    auto Finalize() && noexcept -> std::optional<std::string> final {
        if (initialized_) {
            auto out = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>{};
            if (SHA256_Final(out.data(), &ctx_) == 1) {
                return std::string{out.begin(), out.end()};
            }
        }
        return std::nullopt;
    }

    auto Compute(std::string const& data) && noexcept -> std::string final {
        if (Update(data)) {
            auto digest = std::move(*this).Finalize();
            if (digest) {
                return *digest;
            }
        }
        FatalError();
        return {};
    }

    [[nodiscard]] auto DigestLength() const noexcept -> std::size_t final {
        return SHA256_DIGEST_LENGTH;
    }

  private:
    SHA256_CTX ctx_{};
    bool initialized_{};
};

/// \brief Factory for SHA-256 implementation
auto CreateHashImplSha256() -> std::unique_ptr<IHashImpl> {
    return std::make_unique<HashImplSha256>();
}
