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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_EXECUTION_SERVICE_CAS_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_EXECUTION_SERVICE_CAS_UTILS_HPP

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "grpcpp/support/status.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/storage/storage.hpp"

class CASUtils {
  public:
    [[nodiscard]] static auto EnsureTreeInvariant(
        std::string const& data,
        std::string const& hash,
        Storage const& storage) noexcept -> std::optional<std::string>;

    [[nodiscard]] static auto SplitBlobIdentity(
        bazel_re::Digest const& blob_digest,
        Storage const& storage) noexcept
        -> std::variant<std::vector<bazel_re::Digest>, grpc::Status>;

    [[nodiscard]] static auto SplitBlobFastCDC(
        bazel_re::Digest const& blob_digest,
        Storage const& storage) noexcept
        -> std::variant<std::vector<bazel_re::Digest>, grpc::Status>;

    [[nodiscard]] static auto SpliceBlob(
        bazel_re::Digest const& blob_digest,
        std::vector<bazel_re::Digest> const& chunk_digests,
        Storage const& storage,
        bool check_tree_invariant) noexcept
        -> std::variant<bazel_re::Digest, grpc::Status>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_EXECUTION_SERVICE_CAS_UTILS_HPP
