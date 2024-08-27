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

#include "src/buildtool/build_engine/base_maps/source_map.hpp"

#include <filesystem>
#include <utility>  // std::move

#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/utils/cpp/json.hpp"

namespace BuildMaps::Base {

namespace {

auto as_target(const BuildMaps::Base::EntityName& key,
               ExpressionPtr artifact) -> AnalysedTargetPtr {
    auto stage = ExpressionPtr{
        Expression::map_t{key.GetNamedTarget().name, std::move(artifact)}};
    return std::make_shared<AnalysedTarget const>(
        TargetResult{.artifact_stage = stage,
                     .provides = Expression::kEmptyMap,
                     .runfiles = stage},
        std::vector<ActionDescription::Ptr>{},
        std::vector<std::string>{},
        std::vector<Tree::Ptr>{},
        std::unordered_set<std::string>{},
        std::set<std::string>{},
        std::set<std::string>{},
        TargetGraphInformation::kSource);
}

}  // namespace

auto CreateSourceTargetMap(
    const gsl::not_null<DirectoryEntriesMap*>& dirs,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs) -> SourceTargetMap {
    auto src_target_reader = [dirs, repo_config](auto ts,
                                                 auto setter,
                                                 auto logger,
                                                 auto /* unused */,
                                                 auto const& key) {
        using std::filesystem::path;
        const auto& target = key.GetNamedTarget();
        auto name = path(target.name).lexically_normal();
        if (name.is_absolute() or *name.begin() == "..") {
            (*logger)(
                fmt::format("Source file reference outside current module: {}",
                            target.name),
                true);
            return;
        }
        auto dir = (path(target.module) / name).parent_path();
        auto const* ws_root = repo_config->WorkspaceRoot(target.repository);

        auto src_file_reader = [key, name, setter, logger, dir, ws_root](
                                   bool exists_in_ws_root) {
            if (ws_root != nullptr and exists_in_ws_root) {
                if (auto desc = ws_root->ToArtifactDescription(
                        path(key.GetNamedTarget().module) / name,
                        key.GetNamedTarget().repository)) {
                    (*setter)(as_target(key, ExpressionPtr{std::move(*desc)}));
                    return;
                }
            }
            (*logger)(
                fmt::format(
                    "Cannot determine source file {} in directory {} of "
                    "repository {}",
                    nlohmann::json(
                        path(key.GetNamedTarget().name).filename().string())
                        .dump(),
                    nlohmann::json(dir.string()).dump(),
                    nlohmann::json(key.GetNamedTarget().repository).dump()),
                true);
        };

        if (ws_root != nullptr and ws_root->HasFastDirectoryLookup()) {
            // by-pass directory map and directly attempt to read from ws_root
            src_file_reader(ws_root->IsBlob(path(target.module) / name));
            return;
        }
        dirs->ConsumeAfterKeysReady(
            ts,
            {ModuleName{target.repository, dir.string()}},
            [key, src_file_reader](auto values) {
                src_file_reader(values[0]->ContainsBlob(
                    path(key.GetNamedTarget().name).filename().string()));
            },
            [logger, dir](auto msg, auto fatal) {
                (*logger)(
                    fmt::format("While reading contents of directory {}: {}",
                                nlohmann::json(dir.string()).dump(),
                                msg),
                    fatal);
            }

        );
    };
    return AsyncMapConsumer<EntityName, AnalysedTargetPtr>(src_target_reader,
                                                           jobs);
}

}  // namespace BuildMaps::Base
