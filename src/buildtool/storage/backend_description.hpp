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

#ifndef INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP
#define INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/utils/cpp/expected.hpp"

class BackendDescription final {
  public:
    explicit BackendDescription() noexcept;

    /// \brief String representation of the used execution backend.
    [[nodiscard]] static auto Describe(
        std::optional<ServerAddress> const& address,
        ExecutionProperties const& properties,
        std::vector<DispatchEndpoint> const& dispatch) noexcept
        -> expected<BackendDescription, std::string>;

    [[nodiscard]] auto GetDescription() const noexcept -> std::string const& {
        return *description_;
    }

    [[nodiscard]] auto HashContent(HashFunction hash_function) const noexcept
        -> ArtifactDigest;

    [[nodiscard]] auto operator==(
        BackendDescription const& other) const noexcept -> bool {
        return sha256_ == other.sha256_ or *sha256_ == *other.sha256_;
    }

  private:
    explicit BackendDescription(std::shared_ptr<std::string> description,
                                std::shared_ptr<std::string> sha256) noexcept
        : description_{std::move(description)}, sha256_{std::move(sha256)} {}

    std::shared_ptr<std::string> description_;
    std::shared_ptr<std::string> sha256_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_STORAGE_BACKEND_DESCRIPTION_HPP
