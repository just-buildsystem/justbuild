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

#include <exception>
#include <limits>
#include <string_view>
#include <variant>

#include "gsl/gsl"
#include "openssl/sha.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

// Since it is impossible either forward declare SHA*_CTX (they are typedefs),
// nor their basic classes (they are called differently in OpenSSL and
// BoringSSL), an intermediate struct is forward declared in the header.
using VariantContext = std::variant<SHA_CTX, SHA256_CTX, SHA512_CTX>;
struct Hasher::ShaContext final : VariantContext {
    using VariantContext::VariantContext;
};

namespace {
inline constexpr int kOpenSslTrue = 1;

[[nodiscard]] auto CreateShaContext(Hasher::HashType type) noexcept
    -> std::unique_ptr<Hasher::ShaContext> {
    switch (type) {
        case Hasher::HashType::SHA1:
            return std::make_unique<Hasher::ShaContext>(SHA_CTX{});
        case Hasher::HashType::SHA256:
            return std::make_unique<Hasher::ShaContext>(SHA256_CTX{});
        case Hasher::HashType::SHA512:
            return std::make_unique<Hasher::ShaContext>(SHA512_CTX{});
    }
    return nullptr;  // make gcc happy
}

template <typename TVisitor, typename... Args>
[[nodiscard]] auto Visit(gsl::not_null<VariantContext*> const& ctx,
                         Args&&... visitor_args) noexcept {
    try {
        return std::visit(TVisitor{std::forward<Args>(visitor_args)...}, *ctx);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "HashFunction::{} failed with an exception:\n{}",
                    TVisitor::kLogInfo,
                    e.what());
        Ensures(false);
    }
}

struct InitializeVisitor final {
    static constexpr std::string_view kLogInfo = "Initialize";

    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA_CTX& ctx) const -> bool {
        return SHA1_Init(&ctx) == kOpenSslTrue;
    }
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA256_CTX& ctx) const -> bool {
        return SHA256_Init(&ctx) == kOpenSslTrue;
    }
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA512_CTX& ctx) const -> bool {
        return SHA512_Init(&ctx) == kOpenSslTrue;
    }
};

struct UpdateVisitor final {
    static constexpr std::string_view kLogInfo = "Update";

    explicit UpdateVisitor(gsl::not_null<std::string const*> const& data)
        : data_{*data} {}

    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA_CTX& ctx) const -> bool {
        return SHA1_Update(&ctx, data_.data(), data_.size()) == kOpenSslTrue;
    }
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA256_CTX& ctx) const -> bool {
        return SHA256_Update(&ctx, data_.data(), data_.size()) == kOpenSslTrue;
    }
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] auto operator()(SHA512_CTX& ctx) const -> bool {
        return SHA512_Update(&ctx, data_.data(), data_.size()) == kOpenSslTrue;
    }

  private:
    std::string const& data_;
};

struct FinalizeVisitor final {
    static constexpr std::string_view kLogInfo = "Finalize";

    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] inline auto operator()(SHA_CTX& ctx) const
        -> std::optional<std::string>;
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] inline auto operator()(SHA256_CTX& ctx) const
        -> std::optional<std::string>;
    // NOLINTNEXTLINE(google-runtime-references)
    [[nodiscard]] inline auto operator()(SHA512_CTX& ctx) const
        -> std::optional<std::string>;
};

struct LengthVisitor final {
    static constexpr std::string_view kLogInfo = "GetHashLength";

    [[nodiscard]] constexpr auto operator()(SHA_CTX const& /*unused*/) const
        -> std::size_t {
        return SHA_DIGEST_LENGTH * kCharsPerNumber;
    }
    [[nodiscard]] constexpr auto operator()(SHA256_CTX const& /*unused*/) const
        -> std::size_t {
        return SHA256_DIGEST_LENGTH * kCharsPerNumber;
    }
    [[nodiscard]] constexpr auto operator()(SHA512_CTX const& /*unused*/) const
        -> std::size_t {
        return SHA512_DIGEST_LENGTH * kCharsPerNumber;
    }

  private:
    static constexpr size_t kCharsPerNumber =
        std::numeric_limits<std::uint8_t>::max() /
        std::numeric_limits<signed char>::max();
};
}  // namespace

Hasher::Hasher(std::unique_ptr<ShaContext> sha_ctx) noexcept
    : sha_ctx_{std::move(sha_ctx)} {}

// Explicitly declared and then defaulted dtor and move ctor/operator are needed
// to compile std::unique_ptr of an incomplete type.
Hasher::Hasher(Hasher&& other) noexcept = default;
auto Hasher::operator=(Hasher&& other) noexcept -> Hasher& = default;
Hasher::~Hasher() noexcept = default;

auto Hasher::Create(HashType type) noexcept -> std::optional<Hasher> {
    auto sha_ctx = CreateShaContext(type);
    if (sha_ctx != nullptr and Visit<InitializeVisitor>(sha_ctx.get())) {
        return std::optional<Hasher>{Hasher{std::move(sha_ctx)}};
    }
    return std::nullopt;
}

auto Hasher::Update(std::string const& data) noexcept -> bool {
    return Visit<UpdateVisitor>(sha_ctx_.get(), &data);
}

auto Hasher::Finalize() && noexcept -> HashDigest {
    if (auto hash = Visit<FinalizeVisitor>(sha_ctx_.get())) {
        return HashDigest{std::move(*hash)};
    }
    Logger::Log(LogLevel::Error, "Failed to compute hash.");
    Ensures(false);
}

auto Hasher::GetHashLength() const noexcept -> std::size_t {
    return Visit<LengthVisitor>(sha_ctx_.get());
}

namespace {
auto FinalizeVisitor::operator()(SHA_CTX& ctx) const
    -> std::optional<std::string> {
    auto out = std::array<std::uint8_t, SHA_DIGEST_LENGTH>{};
    if (SHA1_Final(out.data(), &ctx) == kOpenSslTrue) {
        return std::string{out.begin(), out.end()};
    }
    return std::nullopt;
}
auto FinalizeVisitor::operator()(SHA256_CTX& ctx) const
    -> std::optional<std::string> {
    auto out = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>{};
    if (SHA256_Final(out.data(), &ctx) == kOpenSslTrue) {
        return std::string{out.begin(), out.end()};
    }
    return std::nullopt;
}
auto FinalizeVisitor::operator()(SHA512_CTX& ctx) const
    -> std::optional<std::string> {
    auto out = std::array<std::uint8_t, SHA512_DIGEST_LENGTH>{};
    if (SHA512_Final(out.data(), &ctx) == kOpenSslTrue) {
        return std::string{out.begin(), out.end()};
    }
    return std::nullopt;
}

}  // namespace
