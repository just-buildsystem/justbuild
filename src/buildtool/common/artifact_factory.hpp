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

#ifndef INCLUDED_SRC_COMMON_ARTIFACT_FACTORY_HPP
#define INCLUDED_SRC_COMMON_ARTIFACT_FACTORY_HPP

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

class ArtifactFactory {
  public:
    [[nodiscard]] static auto Identifier(nlohmann::json const& description)
        -> ArtifactIdentifier {
        auto desc = ArtifactDescription::FromJson(description);
        if (desc) {
            return desc->Id();
        }
        return {};
    }

    [[nodiscard]] static auto FromDescription(nlohmann::json const& description)
        -> std::optional<Artifact> {
        auto desc = ArtifactDescription::FromJson(description);
        if (desc) {
            return desc->ToArtifact();
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto DescribeLocalArtifact(
        std::filesystem::path const& src_path,
        std::string repository) noexcept -> nlohmann::json {
        return ArtifactDescription{src_path, std::move(repository)}.ToJson();
    }

    [[nodiscard]] static auto DescribeKnownArtifact(
        std::string const& blob_id,
        std::size_t size,
        ObjectType type = ObjectType::File) noexcept -> nlohmann::json {
        return ArtifactDescription{
            ArtifactDigest{blob_id, size, IsTreeObject(type)}, type}
            .ToJson();
    }

    [[nodiscard]] static auto DescribeActionArtifact(
        std::string const& action_id,
        std::string const& out_path) noexcept -> nlohmann::json {
        return ArtifactDescription{action_id, std::filesystem::path{out_path}}
            .ToJson();
    }

    [[nodiscard]] static auto DescribeTreeArtifact(
        std::string const& tree_id) noexcept -> nlohmann::json {
        return ArtifactDescription{tree_id}.ToJson();
    }

    [[nodiscard]] static auto DescribeAction(
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::vector<std::string> const& command) noexcept -> nlohmann::json {
        return ActionDescription{
            output_files, output_dirs, Action{"unused", command, {}}, {}}
            .ToJson();
    }

    [[nodiscard]] static auto DescribeAction(
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::vector<std::string> const& command,
        ActionDescription::inputs_t const& input,
        std::map<std::string, std::string> const& env) noexcept
        -> nlohmann::json {
        return ActionDescription{
            output_files, output_dirs, Action{"unused", command, env}, input}
            .ToJson();
    }

    [[nodiscard]] static auto IsLocal(nlohmann::json const& description)
        -> bool {
        return description.at("type") == "LOCAL";
    }
};  // class ArtifactFactory

#endif  // INCLUDED_SRC_COMMON_ARTIFACT_FACTORY_HPP
