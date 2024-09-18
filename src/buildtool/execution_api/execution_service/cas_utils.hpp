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

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "grpcpp/support/status.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"

class CASUtils {
  public:
    [[nodiscard]] static auto AddDataToCAS(ArtifactDigest const& digest,
                                           std::string const& content,
                                           Storage const& storage) noexcept
        -> grpc::Status;

    [[nodiscard]] static auto AddFileToCAS(ArtifactDigest const& digest,
                                           std::filesystem::path const& file,
                                           Storage const& storage,
                                           bool is_owner = true) noexcept
        -> grpc::Status;

    [[nodiscard]] static auto SplitBlobIdentity(
        ArtifactDigest const& blob_digest,
        Storage const& storage) noexcept
        -> expected<std::vector<ArtifactDigest>, grpc::Status>;

    [[nodiscard]] static auto SplitBlobFastCDC(
        ArtifactDigest const& blob_digest,
        Storage const& storage) noexcept
        -> expected<std::vector<ArtifactDigest>, grpc::Status>;

    [[nodiscard]] static auto SpliceBlob(
        ArtifactDigest const& blob_digest,
        std::vector<ArtifactDigest> const& chunk_digests,
        Storage const& storage) noexcept
        -> expected<ArtifactDigest, grpc::Status>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_EXECUTION_SERVICE_CAS_UTILS_HPP
