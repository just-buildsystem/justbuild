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

#include <functional>
#include <memory>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/serve/mr_git_api.hpp"
#include "src/buildtool/execution_api/utils/rehash_utils.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"

auto CheckServeHasAbsentRoot(ServeApi const& serve,
                             std::string const& tree_id,
                             AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<bool> {
    if (auto has_tree = serve.CheckRootTree(tree_id)) {
        return has_tree;
    }
    (*logger)(fmt::format("Checking that the serve endpoint knows tree "
                          "{} failed.",
                          tree_id),
              /*fatal=*/true);
    return std::nullopt;
}

auto EnsureAbsentRootOnServe(
    ServeApi const& serve,
    std::string const& tree_id,
    std::filesystem::path const& repo_path,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    AsyncMapConsumerLoggerPtr const& logger,
    bool no_sync_is_fatal) -> bool {
    auto const native_digest = ArtifactDigestFactory::Create(
        HashFunction::Type::GitSHA1, tree_id, 0, /*is_tree=*/true);
    if (not native_digest) {
        (*logger)(fmt::format("Failed to create digest for {}", tree_id),
                  /*fatal=*/true);
        return false;
    }
    // check if upload is required
    if (remote_api != nullptr) {
        // upload tree to remote CAS
        auto repo = RepositoryConfig{};
        if (not repo.SetGitCAS(repo_path)) {
            (*logger)(
                fmt::format("Failed to SetGitCAS at {}", repo_path.string()),
                /*fatal=*/true);
            return false;
        }
        auto git_api = MRGitApi{&repo,
                                native_storage_config,
                                compat_storage_config,
                                compat_storage,
                                local_api};
        if (not git_api.RetrieveToCas(
                {Artifact::ObjectInfo{.digest = *native_digest,
                                      .type = ObjectType::Tree}},
                *remote_api)) {
            (*logger)(fmt::format("Failed to sync tree {} from repository {}",
                                  tree_id,
                                  repo_path.string()),
                      /*fatal=*/true);
            return false;
        }
    }
    // ask serve endpoint to retrieve the uploaded tree; this can only happen if
    // we have access to a digest that the remote knows
    ArtifactDigest remote_digest = *native_digest;
    if (compat_storage_config != nullptr) {
        // in compatible mode, get compatible digest from mapping, if exists
        auto cached_obj =
            RehashUtils::ReadRehashedDigest(*native_digest,
                                            *native_storage_config,
                                            *compat_storage_config,
                                            /*from_git=*/true);
        if (not cached_obj) {
            (*logger)(cached_obj.error(), /*fatal=*/true);
            return false;
        }
        if (not *cached_obj) {
            // digest is not known; respond based on no_sync_is_fatal flag
            (*logger)(fmt::format("No digest provided to sync root tree {}.",
                                  tree_id),
                      /*fatal=*/no_sync_is_fatal);
            return not no_sync_is_fatal;
        }
        remote_digest = cached_obj->value().digest;
    }
    if (not serve.GetTreeFromRemote(remote_digest)) {
        // respond based on no_sync_is_fatal flag
        (*logger)(
            fmt::format("Serve endpoint failed to sync root tree {}.", tree_id),
            /*fatal=*/no_sync_is_fatal);
        return not no_sync_is_fatal;
    }
    // done!
    return true;
}
