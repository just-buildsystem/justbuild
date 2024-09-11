// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/common/bazel_digest_factory.hpp"

#include <utility>

#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hasher.hpp"

auto BazelDigestFactory::Create(HashInfo const& hash_info,
                                std::int64_t size) noexcept
    -> bazel_re::Digest {
    auto hash = ProtocolTraits::IsNative(hash_info.HashType())
                    ? Prefix(hash_info.Hash(), hash_info.IsTree())
                    : hash_info.Hash();

    bazel_re::Digest digest{};
    digest.set_hash(std::move(hash));
    digest.set_size_bytes(size);
    return digest;
}

auto BazelDigestFactory::ToHashInfo(HashFunction::Type hash_type,
                                    bazel_re::Digest const& digest) noexcept
    -> expected<HashInfo, std::string> {
    bool const is_prefixed = IsPrefixed(hash_type, digest.hash());

    auto hash = is_prefixed ? Unprefix(digest.hash()) : digest.hash();
    auto const is_tree = is_prefixed and digest.hash().starts_with(kTreeTag);
    return HashInfo::Create(hash_type, std::move(hash), is_tree);
}

auto BazelDigestFactory::IsPrefixed(HashFunction::Type hash_type,
                                    std::string const& hash) noexcept -> bool {
    auto const tagged_length =
        HashFunction{hash_type}.MakeHasher().GetHashLength() + kTagLength;
    return hash.size() == tagged_length;
}
