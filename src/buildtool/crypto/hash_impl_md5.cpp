#include <array>
#include <cstdint>

#include "openssl/md5.h"
#include "src/buildtool/crypto/hash_impl.hpp"

/// \brief Hash implementation for MD5
class HashImplMd5 final : public IHashImpl {
  public:
    HashImplMd5() { initialized_ = MD5_Init(&ctx_) == 1; }

    auto Update(std::string const& data) noexcept -> bool final {
        return initialized_ and
               MD5_Update(&ctx_, data.data(), data.size()) == 1;
    }

    auto Finalize() && noexcept -> std::optional<std::string> final {
        if (initialized_) {
            auto out = std::array<std::uint8_t, MD5_DIGEST_LENGTH>{};
            if (MD5_Final(out.data(), &ctx_) == 1) {
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
        return MD5_DIGEST_LENGTH;
    }

  private:
    MD5_CTX ctx_{};
    bool initialized_{};
};

/// \brief Factory for MD5 implementation
auto CreateHashImplMd5() -> std::unique_ptr<IHashImpl> {
    return std::make_unique<HashImplMd5>();
}
