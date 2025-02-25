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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "src/buildtool/common/artifact_digest.hpp"

class ArtifactBlob final {
  public:
    explicit ArtifactBlob(ArtifactDigest digest,
                          std::string content,
                          bool is_exec) noexcept
        : digest_{std::move(digest)},
          content_{std::make_shared<std::string const>(std::move(content))},
          is_executable_{is_exec} {}

    [[nodiscard]] auto operator==(ArtifactBlob const& other) const noexcept
        -> bool {
        return digest_ == other.digest_ and
               is_executable_ == other.is_executable_;
    }

    /// \brief Obtain the digest of the content.
    [[nodiscard]] auto GetDigest() const noexcept -> ArtifactDigest const& {
        return digest_;
    }

    /// \brief Obtain the size of the content.
    [[nodiscard]] auto GetContentSize() const noexcept -> std::size_t {
        return content_->size();
    }

    /// \brief Read the content from source.
    [[nodiscard]] auto ReadContent() const noexcept
        -> std::shared_ptr<std::string const>;

    /// \brief Set executable permission.
    void SetExecutable(bool is_executable) noexcept {
        is_executable_ = is_executable;
    }

    /// \brief Obtain executable permission.
    [[nodiscard]] auto IsExecutable() const noexcept -> bool {
        return is_executable_;
    }

  private:
    ArtifactDigest digest_;
    std::shared_ptr<std::string const> content_;
    bool is_executable_;
};

namespace std {
template <>
struct hash<ArtifactBlob> {
    [[nodiscard]] auto operator()(ArtifactBlob const& blob) const noexcept
        -> std::size_t;
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP
