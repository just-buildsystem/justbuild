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

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>  // std::move
#include <variant>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

class ArtifactDescription {
    using Local = std::pair<std::filesystem::path, std::string>;
    using Known =
        std::tuple<ArtifactDigest, ObjectType, std::optional<std::string>>;
    using Action = std::pair<std::string, std::filesystem::path>;
    using Tree = std::string;

  public:
    explicit ArtifactDescription(std::filesystem::path path,
                                 std::string repository) noexcept
        : data_{std::make_pair(std::move(path), std::move(repository))} {}

    ArtifactDescription(ArtifactDigest digest,
                        ObjectType file_type,
                        std::optional<std::string> repo = std::nullopt) noexcept
        : data_{
              std::make_tuple(std::move(digest), file_type, std::move(repo))} {}

    ArtifactDescription(std::string action_id,
                        std::filesystem::path path) noexcept
        : data_{std::make_pair(std::move(action_id), std::move(path))} {}

    explicit ArtifactDescription(std::string tree_id) noexcept
        : data_{std::move(tree_id)} {}

    [[nodiscard]] static auto FromJson(nlohmann::json const& json) noexcept
        -> std::optional<ArtifactDescription> {
        try {
            auto const type = ExtractValueAs<std::string>(
                json, "type", [](std::string const& error) {
                    Logger::Log(
                        LogLevel::Error,
                        "{}\ncan not retrieve value for \"type\" from artifact "
                        "description.",
                        error);
                });
            auto const data = ExtractValueAs<nlohmann::json>(
                json, "data", [](std::string const& error) {
                    Logger::Log(
                        LogLevel::Error,
                        "{}\ncan not retrieve value for \"data\" from artifact "
                        "description.",
                        error);
                });

            if (not(type and data)) {
                return std::nullopt;
            }

            if (*type == "LOCAL") {
                return CreateLocalArtifactDescription(*data);
            }
            if (*type == "KNOWN") {
                return CreateKnownArtifactDescription(*data);
            }
            if (*type == "ACTION") {
                return CreateActionArtifactDescription(*data);
            }
            if (*type == "TREE") {
                return CreateTreeArtifactDescription(*data);
            }
            Logger::Log(LogLevel::Error,
                        R"(artifact type must be one of "LOCAL", "KNOWN",
                        "ACTION", or "TREE")");
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "Failed to parse artifact description from JSON with "
                        "error:\n{}",
                        ex.what());
        }
        return std::nullopt;
    }

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

    [[nodiscard]] auto ToJson() const noexcept -> nlohmann::json {
        try {
            if (std::holds_alternative<Local>(data_)) {
                auto const& [path, repo] = std::get<Local>(data_);
                return DescribeLocalArtifact(path.string(), repo);
            }
            if (std::holds_alternative<Known>(data_)) {
                auto const& [digest, file_type, _] = std::get<Known>(data_);
                return DescribeKnownArtifact(
                    digest.hash(), digest.size(), file_type);
            }
            if (std::holds_alternative<Action>(data_)) {
                auto const& [action_id, path] = std::get<Action>(data_);
                return DescribeActionArtifact(action_id, path);
            }
            if (std::holds_alternative<Tree>(data_)) {
                return DescribeTreeArtifact(std::get<Tree>(data_));
            }
            Logger::Log(LogLevel::Error,
                        "Internal error, unknown artifact type");
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "Serializing to JSON failed with error:\n{}",
                        ex.what());
        }
        Ensures(false);  // unreachable
        return {};
    }

    [[nodiscard]] auto ToArtifact() const noexcept -> Artifact {
        try {
            if (std::holds_alternative<Local>(data_)) {
                auto const& [path, repo] = std::get<Local>(data_);
                return Artifact::CreateLocalArtifact(id_, path.string(), repo);
            }
            if (std::holds_alternative<Known>(data_)) {
                auto const& [digest, file_type, repo] = std::get<Known>(data_);
                return Artifact::CreateKnownArtifact(
                    id_, digest.hash(), digest.size(), file_type, repo);
            }
            if (std::holds_alternative<Action>(data_) or
                std::holds_alternative<Tree>(data_)) {
                return Artifact::CreateActionArtifact(id_);
            }
            Logger::Log(LogLevel::Error,
                        "Internal error, unknown artifact type");
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "Creating artifact failed with error:\n{}",
                        ex.what());
        }
        Ensures(false);  // unreachable
        return Artifact{{}};
    }

    [[nodiscard]] auto ToString(int indent = 0) const noexcept -> std::string {
        try {
            return ToJson().dump(indent);
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "Serializing artifact failed with error:\n{}",
                        ex.what());
        }
        return {};
    }

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
    ArtifactIdentifier id_{ComputeId(ToJson())};

    [[nodiscard]] static auto ComputeId(nlohmann::json const& desc) noexcept
        -> ArtifactIdentifier {
        try {
            return HashFunction::ComputeHash(desc.dump()).Bytes();
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "Computing artifact id failed with error:\n{}",
                        ex.what());
        }
        return {};
    }

    [[nodiscard]] static auto DescribeLocalArtifact(
        std::filesystem::path const& src_path,
        std::string const& repository) noexcept -> nlohmann::json {
        return {{"type", "LOCAL"},
                {"data",
                 {{"path", src_path.string()}, {"repository", repository}}}};
    }

    [[nodiscard]] static auto DescribeKnownArtifact(
        std::string const& blob_id,
        std::size_t size,
        ObjectType type = ObjectType::File) noexcept -> nlohmann::json {
        std::string const typestr{ToChar(type)};
        return {{"type", "KNOWN"},
                {"data",
                 {{"id", blob_id}, {"size", size}, {"file_type", typestr}}}};
    }

    [[nodiscard]] static auto DescribeActionArtifact(
        std::string const& action_id,
        std::string const& out_path) noexcept -> nlohmann::json {
        return {{"type", "ACTION"},
                {"data", {{"id", action_id}, {"path", out_path}}}};
    }

    [[nodiscard]] static auto DescribeTreeArtifact(
        std::string const& tree_id) noexcept -> nlohmann::json {
        return {{"type", "TREE"}, {"data", {{"id", tree_id}}}};
    }

    [[nodiscard]] static auto CreateLocalArtifactDescription(
        nlohmann::json const& data) -> std::optional<ArtifactDescription> {

        auto const path = ExtractValueAs<std::string>(
            data, "path", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"path\" from "
                            "LOCAL artifact's data.",
                            error);
            });
        auto const repository = ExtractValueAs<std::string>(
            data, "repository", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"path\" from "
                            "LOCAL artifact's data.",
                            error);
            });
        if (path.has_value() and repository.has_value()) {
            return ArtifactDescription{std::filesystem::path{*path},
                                       *repository};
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto CreateKnownArtifactDescription(
        nlohmann::json const& data) -> std::optional<ArtifactDescription> {

        auto const blob_id = ExtractValueAs<std::string>(
            data, "id", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"id\" from "
                            "KNOWN artifact's data.",
                            error);
            });
        auto const size = ExtractValueAs<std::size_t>(
            data, "size", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"size\" from "
                            "KNOWN artifact's data.",
                            error);
            });
        auto const file_type = ExtractValueAs<std::string>(
            data, "file_type", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"file_type\" from "
                            "KNOWN artifact's data.",
                            error);
            });
        if (blob_id.has_value() and size.has_value() and
            file_type.has_value() and file_type->size() == 1) {
            auto const& object_type = FromChar((*file_type)[0]);
            return ArtifactDescription{
                ArtifactDigest{*blob_id, *size, IsTreeObject(object_type)},
                object_type};
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto CreateActionArtifactDescription(
        nlohmann::json const& data) -> std::optional<ArtifactDescription> {

        auto const action_id = ExtractValueAs<std::string>(
            data, "id", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"id\" from "
                            "ACTION artifact's data.",
                            error);
            });

        auto const path = ExtractValueAs<std::string>(
            data, "path", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"path\" from "
                            "ACTION artifact's data.",
                            error);
            });
        if (action_id.has_value() and path.has_value()) {
            return ArtifactDescription{*action_id,
                                       std::filesystem::path{*path}};
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto CreateTreeArtifactDescription(
        nlohmann::json const& data) -> std::optional<ArtifactDescription> {
        auto const tree_id = ExtractValueAs<std::string>(
            data, "id", [](std::string const& error) {
                Logger::Log(LogLevel::Error,
                            "{}\ncan not retrieve value for \"id\" from "
                            "TREE artifact's data.",
                            error);
            });

        if (tree_id.has_value()) {
            return ArtifactDescription{*tree_id};
        }
        return std::nullopt;
    }
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
