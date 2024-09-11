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

#include "src/other_tools/root_maps/root_utils.hpp"

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/git/git_api.hpp"
#include "src/buildtool/file_system/object_type.hpp"

auto CheckServeHasAbsentRoot(ServeApi const& serve,
                             std::string const& tree_id,
                             AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<bool> {
    if (auto has_tree = serve.CheckRootTree(tree_id)) {
        return *has_tree;
    }
    (*logger)(fmt::format("Checking that the serve endpoint knows tree "
                          "{} failed.",
                          tree_id),
              /*fatal=*/true);
    return std::nullopt;
}

auto EnsureAbsentRootOnServe(ServeApi const& serve,
                             std::string const& tree_id,
                             std::filesystem::path const& repo_path,
                             IExecutionApi const* remote_api,
                             AsyncMapConsumerLoggerPtr const& logger,
                             bool no_sync_is_fatal) -> bool {
    if (remote_api != nullptr) {
        // upload tree to remote CAS
        auto repo = RepositoryConfig{};
        if (not repo.SetGitCAS(repo_path)) {
            (*logger)(
                fmt::format("Failed to SetGitCAS at {}", repo_path.string()),
                /*fatal=*/true);
            return false;
        }
        auto const digest = ArtifactDigestFactory::Create(
            HashFunction::Type::GitSHA1, tree_id, 0, /*is_tree=*/true);

        auto git_api = GitApi{&repo};
        if (not digest or not git_api.RetrieveToCas(
                              {Artifact::ObjectInfo{.digest = *digest,
                                                    .type = ObjectType::Tree}},
                              *remote_api)) {
            (*logger)(fmt::format("Failed to sync tree {} from repository {}",
                                  tree_id,
                                  repo_path.string()),
                      /*fatal=*/true);
            return false;
        }
    }
    // ask serve endpoint to retrieve the uploaded tree
    if (not serve.GetTreeFromRemote(tree_id)) {
        // respond based on no_sync_is_fatal flag
        (*logger)(
            fmt::format("Serve endpoint failed to sync root tree {}.", tree_id),
            /*fatal=*/no_sync_is_fatal);
        return not no_sync_is_fatal;
    }
    // done!
    return true;
}
