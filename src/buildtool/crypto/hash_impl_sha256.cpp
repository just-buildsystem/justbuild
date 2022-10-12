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

#include "src/buildtool/crypto/hash_impl_sha256.hpp"

#include <array>
#include <cstdint>

#include "openssl/sha.h"

/// \brief Hash implementation for SHA-256
class HashImplSha256 final : public Hasher::IHashImpl {
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

  private:
    SHA256_CTX ctx_{};
    bool initialized_{};
};

/// \brief Factory for SHA-256 implementation
auto CreateHashImplSha256() -> std::unique_ptr<Hasher::IHashImpl> {
    return std::make_unique<HashImplSha256>();
}
