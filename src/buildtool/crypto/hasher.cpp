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

#include "src/buildtool/crypto/hasher.hpp"

#include "src/buildtool/crypto/hash_impl_sha1.hpp"
#include "src/buildtool/crypto/hash_impl_sha256.hpp"
#include "src/buildtool/crypto/hash_impl_sha512.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace {
[[nodiscard]] auto CreateHashImpl(Hasher::HashType type) noexcept
    -> std::unique_ptr<Hasher::IHashImpl> {
    switch (type) {
        case Hasher::HashType::SHA1:
            return CreateHashImplSha1();
        case Hasher::HashType::SHA256:
            return CreateHashImplSha256();
        case Hasher::HashType::SHA512:
            return CreateHashImplSha512();
    }
    return nullptr;  // make gcc happy
}
}  // namespace

Hasher::Hasher(std::unique_ptr<IHashImpl> impl) noexcept
    : impl_{std::move(impl)} {}

auto Hasher::Create(HashType type) noexcept -> std::optional<Hasher> {
    auto impl = CreateHashImpl(type);
    if (impl == nullptr) {
        return std::nullopt;
    }
    return Hasher{std::move(impl)};
}

auto Hasher::Update(std::string const& data) noexcept -> bool {
    return impl_->Update(data);
}

auto Hasher::Finalize() && noexcept -> HashDigest {
    auto hash = std::move(*impl_).Finalize();
    if (not hash) {
        Logger::Log(LogLevel::Error, "Failed to compute hash.");
        std::terminate();
    }
    return HashDigest{std::move(*hash)};
}

auto Hasher::GetHashLength() const noexcept -> std::size_t {
    return impl_->GetHashLength();
}
