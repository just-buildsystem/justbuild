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

#include "src/buildtool/common/artifact_description.hpp"

#include <cstddef>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

namespace {
[[nodiscard]] auto DescribeLocalArtifact(std::filesystem::path const& src_path,
                                         std::string const& repository)
    -> nlohmann::json;

[[nodiscard]] auto DescribeKnownArtifact(std::string const& blob_id,
                                         std::size_t size,
                                         ObjectType type = ObjectType::File)
    -> nlohmann::json;

[[nodiscard]] auto DescribeActionArtifact(std::string const& action_id,
                                          std::string const& out_path)
    -> nlohmann::json;

[[nodiscard]] auto DescribeTreeArtifact(std::string const& tree_id)
    -> nlohmann::json;

[[nodiscard]] auto CreateLocalArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription>;

[[nodiscard]] auto CreateKnownArtifactDescription(HashFunction::Type hash_type,
                                                  nlohmann::json const& data)
    -> std::optional<ArtifactDescription>;

[[nodiscard]] auto CreateActionArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription>;

[[nodiscard]] auto CreateTreeArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription>;
}  // namespace

auto ArtifactDescription::CreateLocal(std::filesystem::path path,
                                      std::string repository) noexcept
    -> ArtifactDescription {
    Local data{std::move(path), std::move(repository)};
    return ArtifactDescription{std::move(data)};
}

auto ArtifactDescription::CreateAction(std::string action_id,
                                       std::filesystem::path path) noexcept
    -> ArtifactDescription {
    Action data{std::move(action_id), std::move(path)};
    return ArtifactDescription{std::move(data)};
}

auto ArtifactDescription::CreateKnown(ArtifactDigest digest,
                                      ObjectType file_type,
                                      std::optional<std::string> repo) noexcept
    -> ArtifactDescription {
    Known data{std::move(digest), file_type, std::move(repo)};
    return ArtifactDescription{std::move(data)};
}

auto ArtifactDescription::CreateTree(std::string tree_id) noexcept
    -> ArtifactDescription {
    return ArtifactDescription{std::move(tree_id)};
}

auto ArtifactDescription::FromJson(HashFunction::Type hash_type,
                                   nlohmann::json const& json) noexcept
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
            return CreateKnownArtifactDescription(hash_type, *data);
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

auto ArtifactDescription::ToJson() const -> nlohmann::json {
    if (std::holds_alternative<Local>(data_)) {
        auto const& [path, repo] = std::get<Local>(data_);
        return DescribeLocalArtifact(path.string(), repo);
    }
    if (std::holds_alternative<Known>(data_)) {
        auto const& [digest, file_type, _] = std::get<Known>(data_);
        return DescribeKnownArtifact(digest.hash(), digest.size(), file_type);
    }
    if (std::holds_alternative<Action>(data_)) {
        auto const& [action_id, path] = std::get<Action>(data_);
        return DescribeActionArtifact(action_id, path);
    }
    if (std::holds_alternative<Tree>(data_)) {
        return DescribeTreeArtifact(std::get<Tree>(data_));
    }
    Logger::Log(LogLevel::Error, "Internal error, unknown artifact type");
    Ensures(false);  // unreachable
    return {};
}

auto ArtifactDescription::ToArtifact() const noexcept -> Artifact {
    try {
        if (std::holds_alternative<Local>(data_)) {
            auto const& [path, repo] = std::get<Local>(data_);
            return Artifact::CreateLocalArtifact(id_, path.string(), repo);
        }
        if (std::holds_alternative<Known>(data_)) {
            auto const& [digest, file_type, repo] = std::get<Known>(data_);
            return Artifact::CreateKnownArtifact(id_, digest, file_type, repo);
        }
        if (std::holds_alternative<Action>(data_) or
            std::holds_alternative<Tree>(data_)) {
            return Artifact::CreateActionArtifact(id_);
        }
        Logger::Log(LogLevel::Error, "Internal error, unknown artifact type");
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Creating artifact failed with error:\n{}",
                    ex.what());
    }
    Ensures(false);  // unreachable
    return Artifact{{}};
}

auto ArtifactDescription::ToString(int indent) const noexcept -> std::string {
    try {
        return ToJson().dump(indent);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Serializing artifact failed with error:\n{}",
                    ex.what());
    }
    return {};
}

auto ArtifactDescription::ComputeId(nlohmann::json const& desc) noexcept
    -> ArtifactIdentifier {
    try {
        // The type of HashFunction is irrelevant here. It is used for
        // identification and quick comparison of descriptions. SHA256 is used.
        return HashFunction{HashFunction::Type::PlainSHA256}
            .PlainHashData(desc.dump())
            .Bytes();
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "Computing artifact id failed with error:\n{}",
                    ex.what());
    }
    return {};
}

namespace {
auto DescribeLocalArtifact(std::filesystem::path const& src_path,
                           std::string const& repository) -> nlohmann::json {
    return {
        {"type", "LOCAL"},
        {"data", {{"path", src_path.string()}, {"repository", repository}}}};
}

auto DescribeKnownArtifact(std::string const& blob_id,
                           std::size_t size,
                           ObjectType type) -> nlohmann::json {
    std::string const typestr{ToChar(type)};
    return {
        {"type", "KNOWN"},
        {"data", {{"id", blob_id}, {"size", size}, {"file_type", typestr}}}};
}

auto DescribeActionArtifact(std::string const& action_id,
                            std::string const& out_path) -> nlohmann::json {
    return {{"type", "ACTION"},
            {"data", {{"id", action_id}, {"path", out_path}}}};
}

auto DescribeTreeArtifact(std::string const& tree_id) -> nlohmann::json {
    return {{"type", "TREE"}, {"data", {{"id", tree_id}}}};
}

auto CreateLocalArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription> {
    auto path =
        ExtractValueAs<std::string>(data, "path", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"path\" from "
                        "LOCAL artifact's data.",
                        error);
        });
    auto repository = ExtractValueAs<std::string>(
        data, "repository", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"path\" from "
                        "LOCAL artifact's data.",
                        error);
        });
    if (path.has_value() and repository.has_value()) {
        return ArtifactDescription::CreateLocal(std::move(*path),
                                                std::move(*repository));
    }
    return std::nullopt;
}

auto CreateKnownArtifactDescription(HashFunction::Type hash_type,
                                    nlohmann::json const& data)
    -> std::optional<ArtifactDescription> {
    auto const blob_id =
        ExtractValueAs<std::string>(data, "id", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"id\" from "
                        "KNOWN artifact's data.",
                        error);
        });
    auto const size =
        ExtractValueAs<std::size_t>(data, "size", [](std::string const& error) {
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
    if (blob_id.has_value() and size.has_value() and file_type.has_value() and
        file_type->size() == 1) {
        auto const object_type = FromChar((*file_type)[0]);

        auto digest = ArtifactDigestFactory::Create(
            hash_type, *blob_id, *size, IsTreeObject(object_type));
        if (not digest) {
            return std::nullopt;
        }
        return ArtifactDescription::CreateKnown(*std::move(digest),
                                                object_type);
    }
    return std::nullopt;
}

auto CreateActionArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription> {
    auto action_id =
        ExtractValueAs<std::string>(data, "id", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"id\" from "
                        "ACTION artifact's data.",
                        error);
        });

    auto path =
        ExtractValueAs<std::string>(data, "path", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"path\" from "
                        "ACTION artifact's data.",
                        error);
        });
    if (action_id.has_value() and path.has_value()) {
        return ArtifactDescription::CreateAction(std::move(*action_id),
                                                 std::move(*path));
    }
    return std::nullopt;
}

auto CreateTreeArtifactDescription(nlohmann::json const& data)
    -> std::optional<ArtifactDescription> {
    auto tree_id =
        ExtractValueAs<std::string>(data, "id", [](std::string const& error) {
            Logger::Log(LogLevel::Error,
                        "{}\ncan not retrieve value for \"id\" from "
                        "TREE artifact's data.",
                        error);
        });

    if (tree_id.has_value()) {
        return ArtifactDescription::CreateTree(std::move(*tree_id));
    }
    return std::nullopt;
}
}  // namespace
