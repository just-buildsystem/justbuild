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

#include "src/buildtool/main/add_to_cas.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto AddArtifactsToCas(ToAddArguments const& clargs,
                       Storage const& storage,
                       ApiBundle const& apis) -> bool {
    auto object_location = clargs.location;
    if (clargs.follow_symlinks) {
        if (not FileSystemManager::ResolveSymlinks(&object_location)) {
            Logger::Log(LogLevel::Error,
                        "Failed resolving {}",
                        clargs.location.string());
            return false;
        }
    }

    auto object_type =
        FileSystemManager::Type(object_location, /*allow_upwards=*/true);
    if (not object_type) {
        Logger::Log(LogLevel::Error,
                    "Non existent or unsupported file-system entry at {}",
                    object_location.string());
        return false;
    }

    auto const& cas = storage.CAS();
    std::optional<ArtifactDigest> digest{};
    switch (*object_type) {
        case ObjectType::File:
            digest = cas.StoreBlob(object_location, /*is_executable=*/false);
            break;
        case ObjectType::Executable:
            digest = cas.StoreBlob(object_location, /*is_executable=*/true);
            break;
        case ObjectType::Symlink: {
            auto content = FileSystemManager::ReadSymlink(object_location);
            if (not content) {
                Logger::Log(LogLevel::Error,
                            "Failed to read symlink at {}",
                            object_location.string());
                return false;
            }
            digest = cas.StoreBlob(*content, /*is_executable=*/false);
        } break;
        case ObjectType::Tree: {
            if (ProtocolTraits::Instance().IsCompatible()) {
                Logger::Log(LogLevel::Error,
                            "Storing of trees only supported in native mode");
                return false;
            }
            auto store_blob =
                [&cas](std::filesystem::path const& path,
                       auto is_exec) -> std::optional<ArtifactDigest> {
                return cas.StoreBlob</*kOwner=*/true>(path, is_exec);
            };
            auto store_tree = [&cas](std::string const& content)
                -> std::optional<ArtifactDigest> {
                return cas.StoreTree(content);
            };
            auto store_symlink = [&cas](std::string const& content)
                -> std::optional<ArtifactDigest> {
                return cas.StoreBlob(content);
            };
            digest = BazelMsgFactory::CreateGitTreeDigestFromLocalTree(
                clargs.location, store_blob, store_tree, store_symlink);
        } break;
    }

    if (not digest) {
        Logger::Log(LogLevel::Error,
                    "Failed to store {} in local CAS",
                    clargs.location.string());
        return false;
    }

    std::cout << digest->hash() << std::endl;

    auto const object = std::vector<Artifact::ObjectInfo>{
        Artifact::ObjectInfo{*digest, *object_type, false}};

    if (not apis.local->RetrieveToCas(object, *apis.remote)) {
        Logger::Log(LogLevel::Error,
                    "Failed to upload artifact to remote endpoint");
        return false;
    }

    return true;
}

#endif
