#include "src/buildtool/crypto/hash_impl_sha1.hpp"

#include <array>
#include <cstdint>

#include "openssl/sha.h"

/// \brief Hash implementation for SHA-1
class HashImplSha1 final : public Hasher::IHashImpl {
  public:
    HashImplSha1() { initialized_ = SHA1_Init(&ctx_) == 1; }

    auto Update(std::string const& data) noexcept -> bool final {
        return initialized_ and
               SHA1_Update(&ctx_, data.data(), data.size()) == 1;
    }

    auto Finalize() && noexcept -> std::optional<std::string> final {
        if (initialized_) {
            auto out = std::array<std::uint8_t, SHA_DIGEST_LENGTH>{};
            if (SHA1_Final(out.data(), &ctx_) == 1) {
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

  private:
    SHA_CTX ctx_{};
    bool initialized_{};
};

/// \brief Factory for SHA-1 implementation
auto CreateHashImplSha1() -> std::unique_ptr<Hasher::IHashImpl> {
    return std::make_unique<HashImplSha1>();
}
