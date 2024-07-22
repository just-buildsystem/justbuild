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

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/common/tree_reader_utils.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto RetrieveSubPathId(Artifact::ObjectInfo object_info,
                       IExecutionApi const& api,
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
        auto data = api.RetrieveToMemory(object_info);
        if (not data) {
            Logger::Log(LogLevel::Error,
                        "Failed to retrieve artifact {} at path '{}'",
                        object_info.ToString(),
                        sofar.string());
            return std::nullopt;
        }
        if (Compatibility::IsCompatible()) {
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
                    [&new_object_info, &segment](auto path, auto info) {
                        if (path == segment) {
                            new_object_info = info;
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
            auto const hash = HashFunction{HashFunction::Type::GitSHA1}
                                  .HashTreeData(*data)
                                  .Bytes();
            auto entries = GitRepo::ReadTreeData(
                *data,
                hash,
                [](auto const& /*unused*/) { return true; },
                /*is_hex_id=*/false);
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
                    [&new_object_info, &segment](auto path, auto info) {
                        if (path == segment) {
                            new_object_info = info;
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

#endif
