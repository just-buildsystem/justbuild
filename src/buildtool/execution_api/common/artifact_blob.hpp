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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>  //std::move

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/utils/cpp/hash_combine.hpp"

struct ArtifactBlob final {
    ArtifactBlob(ArtifactDigest digest,
                 std::string mydata,
                 bool is_exec) noexcept
        : digest{std::move(digest)},
          data{std::make_shared<std::string>(std::move(mydata))},
          is_exec{is_exec} {}

    [[nodiscard]] auto operator==(ArtifactBlob const& other) const noexcept
        -> bool {
        return digest == other.digest and is_exec == other.is_exec;
    }

    ArtifactDigest digest;
    std::shared_ptr<std::string> data;
    bool is_exec = false;
};

namespace std {
template <>
struct hash<ArtifactBlob> {
    [[nodiscard]] auto operator()(ArtifactBlob const& blob) const noexcept
        -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, blob.digest);
        hash_combine(&seed, blob.is_exec);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP
