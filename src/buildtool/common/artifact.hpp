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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hash_combine.hpp"

// Artifacts (source files, libraries, executables...) need to store their
// identifier
class Artifact {
  public:
    struct ObjectInfo {
        ArtifactDigest digest;
        ObjectType type{};
        bool failed{};

        [[nodiscard]] auto operator==(ObjectInfo const& other) const {
            return (digest == other.digest and type == other.type and
                    failed == other.failed);
        }

        [[nodiscard]] auto operator!=(ObjectInfo const& other) const {
            return not(*this == other);
        }

        // Create string of the form '[hash:size:type]'
        [[nodiscard]] auto ToString(bool size_unknown = false) const noexcept
            -> std::string {
            auto size_str =
                size_unknown ? std::string{} : std::to_string(digest.size());
            return fmt::format("[{}:{}:{}]{}",
                               digest.hash(),
                               size_str,
                               ToChar(type),
                               failed ? " FAILED" : "");
        }

        // Create JSON of the form '{"id": "hash", "size": x, "file_type": "f"}'
        // As the failed property is only internal to a run, discard it.
        [[nodiscard]] auto ToJson() const -> nlohmann::json {
            return {{"id", digest.hash()},
                    {"size", digest.size()},
                    {"file_type", std::string{ToChar(type)}}};
        }

        [[nodiscard]] static auto FromString(HashFunction::Type hash_type,
                                             std::string const& s) noexcept
            -> std::optional<ObjectInfo> {
            std::istringstream iss(s);
            std::string id{};
            std::string size_str{};
            std::string type{};
            if (not(iss.get() == '[') or not std::getline(iss, id, ':') or
                not std::getline(iss, size_str, ':') or
                not std::getline(iss, type, ']')) {
                Logger::Log(LogLevel::Debug,
                            "failed parsing object info from string.");
                return std::nullopt;
            }

            std::size_t size = 0;
            try {
                size = std::stoul(size_str);
            } catch (std::out_of_range const& e) {
                Logger::Log(LogLevel::Debug,
                            "size raised out_of_range exception.");
                return std::nullopt;
            } catch (std::invalid_argument const& e) {
                Logger::Log(LogLevel::Debug,
                            "size raised invalid_argument exception.");
                return std::nullopt;
            }

            auto const object_type = FromChar(*type.c_str());
            auto hash_info =
                HashInfo::Create(hash_type, id, IsTreeObject(object_type));
            if (not hash_info) {
                Logger::Log(
                    LogLevel::Debug, "{}", std::move(hash_info).error());
                return std::nullopt;
            }
            return ObjectInfo{
                .digest = ArtifactDigest{*std::move(hash_info), size},
                .type = object_type};
        }
    };

    explicit Artifact(ArtifactIdentifier id) noexcept : id_{std::move(id)} {}

    Artifact(Artifact const& other) noexcept
        : id_{other.id_}, file_path_{other.file_path_}, repo_{other.repo_} {
        object_info_ = other.object_info_;
    }

    Artifact(Artifact&&) noexcept = default;
    ~Artifact() noexcept = default;
    auto operator=(Artifact const&) noexcept -> Artifact& = delete;
    auto operator=(Artifact&&) noexcept -> Artifact& = default;

    [[nodiscard]] auto Id() const& noexcept -> ArtifactIdentifier const& {
        return id_;
    }

    [[nodiscard]] auto Id() && noexcept -> ArtifactIdentifier {
        return std::move(id_);
    }

    [[nodiscard]] auto FilePath() const noexcept
        -> std::optional<std::filesystem::path> {
        return file_path_;
    }

    [[nodiscard]] auto Repository() const noexcept -> std::string {
        return repo_;
    }

    [[nodiscard]] auto Digest() const noexcept
        -> std::optional<ArtifactDigest> {
        return object_info_ ? std::optional{object_info_->digest}
                            : std::nullopt;
    }

    [[nodiscard]] auto Type() const noexcept -> std::optional<ObjectType> {
        return object_info_ ? std::optional{object_info_->type} : std::nullopt;
    }

    [[nodiscard]] auto Info() const& noexcept
        -> std::optional<ObjectInfo> const& {
        return object_info_;
    }
    [[nodiscard]] auto Info() && noexcept -> std::optional<ObjectInfo> {
        return std::move(object_info_);
    }

    void SetObjectInfo(ObjectInfo const& object_info,
                       bool fail_info) const noexcept {
        if (fail_info) {
            object_info_ = ObjectInfo{.digest = object_info.digest,
                                      .type = object_info.type,
                                      .failed = true};
        }
        else {
            object_info_ = object_info;
        }
    }

    void SetObjectInfo(ArtifactDigest const& digest,
                       ObjectType type,
                       bool failed) const noexcept {
        object_info_ =
            ObjectInfo{.digest = digest, .type = type, .failed = failed};
    }

    [[nodiscard]] static auto CreateLocalArtifact(
        std::string const& id,
        std::filesystem::path const& file_path,
        std::string const& repo) noexcept -> Artifact {
        return Artifact{id, file_path, repo};
    }

    [[nodiscard]] static auto CreateKnownArtifact(
        std::string const& id,
        ArtifactDigest const& digest,
        ObjectType type,
        std::optional<std::string> const& repo) noexcept -> Artifact {
        return Artifact{id, digest, type, false, repo};
    }

    [[nodiscard]] static auto CreateActionArtifact(
        std::string const& id) noexcept -> Artifact {
        return Artifact{id};
    }

  private:
    ArtifactIdentifier id_;
    std::optional<std::filesystem::path> file_path_;
    std::string repo_;
    mutable std::optional<ObjectInfo> object_info_;

    Artifact(ArtifactIdentifier id,
             std::filesystem::path const& file_path,
             std::string repo) noexcept
        : id_{std::move(id)}, file_path_{file_path}, repo_{std::move(repo)} {}

    Artifact(ArtifactIdentifier id,
             ArtifactDigest const& digest,
             ObjectType type,
             bool failed,
             std::optional<std::string> repo) noexcept
        : id_{std::move(id)} {
        SetObjectInfo(digest, type, failed);
        if (repo) {
            repo_ = std::move(*repo);
        }
    }
};

namespace std {
template <>
struct hash<Artifact::ObjectInfo> {
    [[nodiscard]] auto operator()(
        Artifact::ObjectInfo const& info) const noexcept -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, info.digest);
        hash_combine(&seed, info.type);
        hash_combine(&seed, info.failed);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_HPP
