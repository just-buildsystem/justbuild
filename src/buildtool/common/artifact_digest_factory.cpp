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

#include "src/buildtool/common/artifact_digest_factory.hpp"

#include "gsl/gsl"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"

auto ArtifactDigestFactory::Create(HashFunction::Type hash_type,
                                   std::string hash,
                                   std::size_t size,
                                   bool is_tree) noexcept
    -> expected<ArtifactDigest, std::string> {
    auto hash_info =
        HashInfo::Create(hash_type,
                         std::move(hash),
                         ProtocolTraits::IsTreeAllowed(hash_type) and is_tree);
    if (not hash_info) {
        return unexpected{std::move(hash_info).error()};
    }
    return ArtifactDigest{*std::move(hash_info), size};
}

auto ArtifactDigestFactory::FromBazel(HashFunction::Type hash_type,
                                      bazel_re::Digest const& digest) noexcept
    -> expected<ArtifactDigest, std::string> {
    auto hash_info = BazelDigestFactory::ToHashInfo(hash_type, digest);
    if (not hash_info) {
        return unexpected{std::move(hash_info).error()};
    }
    return ArtifactDigest{*std::move(hash_info),
                          static_cast<std::size_t>(digest.size_bytes())};
}

auto ArtifactDigestFactory::ToBazel(ArtifactDigest const& digest)
    -> bazel_re::Digest {
    return BazelDigestFactory::Create(digest.hash_info_,
                                      gsl::narrow<std::int64_t>(digest.size_));
}
