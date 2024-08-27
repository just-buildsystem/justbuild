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

#include "src/buildtool/crypto/hash_info.hpp"

#include "fmt/core.h"
#include "gsl/gsl"  // Ensures
#include "src/buildtool/crypto/hasher.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
[[nodiscard]] inline auto GetHashTypeName(HashFunction::Type type) noexcept
    -> std::string {
    switch (type) {
        case HashFunction::Type::GitSHA1:
            return "GitSHA1";
        case HashFunction::Type::PlainSHA256:
            return "PlainSHA256";
    }
    Ensures(false);
}

inline constexpr auto kSHA1EmptyGitBlobHash =
    "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391";
}  // namespace

HashInfo::HashInfo() noexcept
    : hash_{kSHA1EmptyGitBlobHash},
      hash_type_{HashFunction::Type::GitSHA1},
      is_tree_{false} {}

auto HashInfo::Create(HashFunction::Type type,
                      std::string hash,
                      bool is_tree) noexcept
    -> expected<HashInfo, std::string> {
    if (auto error = HashInfo::ValidateInput(type, hash, is_tree)) {
        return unexpected{*std::move(error)};
    }
    return HashInfo(std::move(hash), type, is_tree);
}

auto HashInfo::HashData(HashFunction hash_function,
                        std::string const& content,
                        bool is_tree) noexcept -> HashInfo {
    auto const hash_digest = is_tree ? hash_function.HashTreeData(content)
                                     : hash_function.HashBlobData(content);
    return HashInfo{
        hash_digest.HexString(),
        hash_function.GetType(),
        is_tree and hash_function.GetType() == HashFunction::Type::GitSHA1};
}

auto HashInfo::HashFile(HashFunction hash_function,
                        std::filesystem::path const& path,
                        bool is_tree) noexcept
    -> std::optional<std::pair<HashInfo, std::uintmax_t>> {
    auto const hash_digest = is_tree ? hash_function.HashTreeFile(path)
                                     : hash_function.HashBlobFile(path);
    if (not hash_digest) {
        return std::nullopt;
    }
    return std::pair{HashInfo{hash_digest->first.HexString(),
                              hash_function.GetType(),
                              is_tree and hash_function.GetType() ==
                                              HashFunction::Type::GitSHA1},
                     hash_digest->second};
}

auto HashInfo::operator==(HashInfo const& other) const noexcept -> bool {
    return hash_ == other.hash_ and is_tree_ == other.is_tree_;
}

auto HashInfo::ValidateInput(HashFunction::Type type,
                             std::string const& hash,
                             bool is_tree) noexcept
    -> std::optional<std::string> {
    if (type != HashFunction::Type::GitSHA1 and is_tree) {
        return fmt::format(
            "HashInfo: hash {} is expected to be {}.\nTrees are "
            "not allowed in this mode.",
            hash,
            GetHashTypeName(type));
    }

    if (auto const exp_size = HashFunction{type}.MakeHasher().GetHashLength();
        hash.size() != exp_size) {
        return fmt::format(
            "HashInfo: hash {} is expected to be {}.\n It must have a length "
            "of {}, but its length is {}.",
            hash,
            GetHashTypeName(type),
            exp_size,
            hash.size());
    }

    if (not IsHexString(hash)) {
        return fmt::format("HashInfo: Invalid hash {}", hash);
    }
    return std::nullopt;
}
