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

#include "src/buildtool/execution_api/common/execution_api.hpp"

#include "gsl/gsl"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[nodiscard]] auto IExecutionApi::UploadFile(
    std::filesystem::path const& file_path,
    ObjectType type) noexcept -> bool {
    auto data = FileSystemManager::ReadFile(file_path);
    if (not data) {
        return false;
    }
    ArtifactDigest digest{};
    switch (type) {
        case ObjectType::File:
            digest = ArtifactDigest::Create<ObjectType::File>(*data);
            break;
        case ObjectType::Executable:
            digest = ArtifactDigest::Create<ObjectType::Executable>(*data);
            break;
        case ObjectType::Tree:
            digest = ArtifactDigest::Create<ObjectType::Tree>(*data);
            break;
        case ObjectType::Symlink:
            digest = ArtifactDigest::Create<ObjectType::Symlink>(*data);
            break;
        default:
            Ensures(false);  // unreachable
            return false;
    }
    BlobContainer container{};
    try {
        auto exec = IsExecutableObject(type);
        container.Emplace(BazelBlob{digest, *data, exec});
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "failed to emplace blob: ", ex.what());
        return false;
    }
    return Upload(container);
}
