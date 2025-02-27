// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/utils/subobject.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL

#include <functional>
#include <memory>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/tree_reader_utils.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto RetrieveSubPathId(Artifact::ObjectInfo object_info,
                       ApiBundle const& apis,
                       const std::filesystem::path& sub_path)
    -> std::optional<Artifact::ObjectInfo> {
    std::filesystem::path sofar{};
    for (auto const& segment : sub_path) {
        if (object_info.type != ObjectType::Tree) {
            Logger::Log(LogLevel::Warning,
                        "Non-tree found at path '{}', cannot follow to '{}'",
                        sofar.string(),
                        segment.string());
            break;
        }
        auto data = apis.remote->RetrieveToMemory(object_info);
        if (not data) {
            Logger::Log(LogLevel::Error,
                        "Failed to retrieve artifact {} at path '{}'",
                        object_info.ToString(),
                        sofar.string());
            return std::nullopt;
        }
        if (not ProtocolTraits::IsNative(apis.remote->GetHashType())) {
            auto directory =
                BazelMsgFactory::MessageFromString<bazel_re::Directory>(*data);
            if (not directory) {
                Logger::Log(LogLevel::Warning,
                            "Failed to parse directory message at path '{}'",
                            sofar.string());
                break;
            }
            std::optional<Artifact::ObjectInfo> new_object_info{};
            if (not TreeReaderUtils::ReadObjectInfos(
                    *directory,
                    [&new_object_info, &segment](
                        std::filesystem::path const& path,
                        Artifact::ObjectInfo&& info) {
                        if (path == segment) {
                            new_object_info = std::move(info);
                        }
                        return true;
                    })) {
                Logger::Log(LogLevel::Warning,
                            "Failed to process directory message at path '{}'",
                            sofar.string());
                break;
            }
            if (not new_object_info) {
                Logger::Log(LogLevel::Warning,
                            "Entry {} not found at path '{}'",
                            segment.string(),
                            sofar.string());
                break;
            }
            object_info = *new_object_info;
        }
        else {
            auto entries = GitRepo::ReadTreeData(
                *data,
                object_info.digest.hash(),
                [](auto const& /*unused*/) { return true; },
                /*is_hex_id=*/true);
            if (not entries) {
                Logger::Log(LogLevel::Warning,
                            "Failed to parse tree {} at path '{}'",
                            object_info.ToString(),
                            sofar.string());
                break;
            }
            std::optional<Artifact::ObjectInfo> new_object_info{};
            if (not TreeReaderUtils::ReadObjectInfos(
                    *entries,
                    [&new_object_info, &segment](
                        std::filesystem::path const& path,
                        Artifact::ObjectInfo&& info) {
                        if (path == segment) {
                            new_object_info = std::move(info);
                        }
                        return true;
                    })) {
                Logger::Log(LogLevel::Warning,
                            "Failed to process tree entries at path '{}'",
                            sofar.string());
                break;
            }

            if (not new_object_info) {
                Logger::Log(LogLevel::Warning,
                            "Entry {} not found at path '{}'",
                            segment.string(),
                            sofar.string());
                break;
            }
            object_info = *new_object_info;
        }
        sofar /= segment;
    }

    return object_info;
}

#endif  // BOOTSTRAP_BUILD_TOOL
