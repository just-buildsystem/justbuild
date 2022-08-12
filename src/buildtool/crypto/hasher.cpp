#include "src/buildtool/crypto/hasher.hpp"

#include "src/buildtool/crypto/hash_impl_sha1.hpp"
#include "src/buildtool/crypto/hash_impl_sha256.hpp"

auto Hasher::CreateHashImpl(HashType type) noexcept
    -> std::unique_ptr<IHashImpl> {
    switch (type) {
        case HashType::SHA1:
            return CreateHashImplSha1();
        case HashType::SHA256:
            return CreateHashImplSha256();
    }
    return nullptr;  // make gcc happy
}
