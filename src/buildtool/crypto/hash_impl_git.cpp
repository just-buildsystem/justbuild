#include <array>
#include <cstdint>

#include "openssl/sha.h"
#include "src/buildtool/crypto/hash_impl.hpp"

/// \brief Hash implementation for Git blob ids.
/// Does not support incremental hashing.
class HashImplGit final : public IHashImpl {
  public:
    auto Update(std::string const& /*data*/) noexcept -> bool final {
        return false;
    }

    auto Finalize() && noexcept -> std::optional<std::string> final {
        return std::nullopt;
    }

    auto Compute(std::string const& data) && noexcept -> std::string final {
        SHA_CTX ctx;
        std::string const header{"blob " + std::to_string(data.size()) + '\0'};
        if (SHA1_Init(&ctx) == 1 &&
            SHA1_Update(&ctx, header.data(), header.size()) == 1 &&
            SHA1_Update(&ctx, data.data(), data.size()) == 1) {
            auto out = std::array<std::uint8_t, SHA_DIGEST_LENGTH>{};
            if (SHA1_Final(out.data(), &ctx) == 1) {
                return std::string{out.begin(), out.end()};
            }
        }
        FatalError();
        return {};
    }

    [[nodiscard]] auto DigestLength() const noexcept -> std::size_t final {
        return SHA_DIGEST_LENGTH;
    }
};

/// \brief Factory for Git implementation
auto CreateHashImplGit() -> std::unique_ptr<IHashImpl> {
    return std::make_unique<HashImplGit>();
}
