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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BYTESTREAM_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BYTESTREAM_UTILS_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace build::bazel::remote::execution::v2 {
class Digest;
}
namespace bazel_re = build::bazel::remote::execution::v2;

class ByteStreamUtils final {
    static constexpr auto* kBlobs = "blobs";

  public:
    // Chunk size for uploads (default size used by BuildBarn)
    static constexpr std::size_t kChunkSize = 64 * 1024;

    /// \brief Create a read request for the bytestream service to be
    /// transferred over the net. Handles serialization/deserialization on its
    /// own. The pattern is:
    /// "{instance_name}/{kBlobs}/{digest.hash()}/{digest.size_bytes()}".
    /// "instance_name_example/blobs/62183d7a696acf7e69e218efc82c93135f8c85f895/4424712"
    class ReadRequest final {
      public:
        explicit ReadRequest(std::string instance_name,
                             bazel_re::Digest const& digest) noexcept;

        [[nodiscard]] auto ToString() && noexcept -> std::string;

        [[nodiscard]] static auto FromString(
            std::string const& request) noexcept -> std::optional<ReadRequest>;

        [[nodiscard]] auto GetInstanceName() const noexcept
            -> std::string const& {
            return instance_name_;
        }

        [[nodiscard]] auto GetDigest() const noexcept -> bazel_re::Digest;

      private:
        std::string instance_name_;
        std::string hash_;
        std::int64_t size_ = 0;

        ReadRequest() = default;
    };
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_BYTESTREAM_UTILS_HPP
