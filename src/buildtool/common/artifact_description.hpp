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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DESCRIPTION_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DESCRIPTION_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <utility>  // std::move
#include <variant>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/object_type.hpp"

class ArtifactDescription final {
    using Local = std::pair<std::filesystem::path, std::string>;
    using Known =
        std::tuple<ArtifactDigest, ObjectType, std::optional<std::string>>;
    using Action = std::pair<std::string, std::filesystem::path>;
    using Tree = std::string;

  public:
    [[nodiscard]] static auto CreateLocal(std::filesystem::path path,
                                          std::string repository) noexcept
        -> ArtifactDescription;

    [[nodiscard]] static auto CreateAction(std::string action_id,
                                           std::filesystem::path path) noexcept
        -> ArtifactDescription;

    [[nodiscard]] static auto CreateKnown(
        ArtifactDigest digest,
        ObjectType file_type,
        std::optional<std::string> repo = std::nullopt) noexcept
        -> ArtifactDescription;

    [[nodiscard]] static auto CreateTree(std::string tree_id) noexcept
        -> ArtifactDescription;

    [[nodiscard]] auto Id() const& noexcept -> ArtifactIdentifier const& {
        return id_;
    }
    [[nodiscard]] auto Id() && noexcept -> ArtifactIdentifier {
        return std::move(id_);
    }

    [[nodiscard]] auto IsKnown() const noexcept -> bool {
        return std::holds_alternative<Known>(data_);
    }

    [[nodiscard]] auto IsTree() const noexcept -> bool {
        return std::holds_alternative<Tree>(data_);
    }

    [[nodiscard]] static auto FromJson(nlohmann::json const& json) noexcept
        -> std::optional<ArtifactDescription>;

    [[nodiscard]] auto ToJson() const noexcept -> nlohmann::json;

    [[nodiscard]] auto ToArtifact() const noexcept -> Artifact;

    [[nodiscard]] auto ToString(int indent = 0) const noexcept -> std::string;

    [[nodiscard]] auto operator==(
        ArtifactDescription const& other) const noexcept -> bool {
        return data_ == other.data_;
    }

    [[nodiscard]] auto operator!=(
        ArtifactDescription const& other) const noexcept -> bool {
        return not(*this == other);
    }

  private:
    std::variant<Local, Known, Action, Tree> data_;
    ArtifactIdentifier id_;

    template <typename T>
    explicit ArtifactDescription(T data) noexcept
        : data_{std::move(data)}, id_{ComputeId(ToJson())} {}

    [[nodiscard]] static auto ComputeId(nlohmann::json const& desc) noexcept
        -> ArtifactIdentifier;
};

namespace std {
template <>
struct hash<ArtifactDescription> {
    [[nodiscard]] auto operator()(ArtifactDescription const& a) const {
        return std::hash<std::string>{}(a.Id());
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_DESCRIPTION_HPP
