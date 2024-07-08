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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP

#ifdef BOOTSTRAP_BUILD_TOOL
class ServeApi final {};
#else

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/file_system/git_types.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/serve_api/remote/config.hpp"
#include "src/buildtool/serve_api/remote/configuration_client.hpp"
#include "src/buildtool/serve_api/remote/source_tree_client.hpp"
#include "src/buildtool/serve_api/remote/target_client.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"

class ServeApi final {
  public:
    explicit ServeApi(ServerAddress const& address,
                      gsl::not_null<Storage const*> const& storage,
                      gsl::not_null<ApiBundle const*> const& apis) noexcept
        : stc_{address, &apis->auth},
          tc_{address, storage, apis},
          cc_{address, &apis->auth, &apis->remote_config} {}

    ~ServeApi() noexcept = default;
    ServeApi(ServeApi const&) = delete;
    ServeApi(ServeApi&&) = delete;
    auto operator=(ServeApi const&) -> ServeApi& = delete;
    auto operator=(ServeApi&&) -> ServeApi& = delete;

    [[nodiscard]] static auto Create(
        RemoteServeConfig const& serve_config,
        gsl::not_null<Storage const*> const& storage,
        gsl::not_null<ApiBundle const*> const& apis) noexcept
        -> std::optional<ServeApi> {
        if (serve_config.remote_address) {
            return std::make_optional<ServeApi>(
                *serve_config.remote_address, storage, apis);
        }
        return std::nullopt;
    }

    [[nodiscard]] auto RetrieveTreeFromCommit(std::string const& commit,
                                              std::string const& subdir = ".",
                                              bool sync_tree = false)
        const noexcept -> expected<std::string, GitLookupError> {
        return stc_.ServeCommitTree(commit, subdir, sync_tree);
    }

    [[nodiscard]] auto RetrieveTreeFromArchive(
        std::string const& content,
        std::string const& archive_type = "archive",
        std::string const& subdir = ".",
        std::optional<PragmaSpecial> const& resolve_symlinks = std::nullopt,
        bool sync_tree = false) const noexcept
        -> expected<std::string, GitLookupError> {
        return stc_.ServeArchiveTree(
            content, archive_type, subdir, resolve_symlinks, sync_tree);
    }

    [[nodiscard]] auto RetrieveTreeFromDistdir(
        std::shared_ptr<std::unordered_map<std::string, std::string>> const&
            distfiles,
        bool sync_tree = false) const noexcept
        -> expected<std::string, GitLookupError> {
        return stc_.ServeDistdirTree(distfiles, sync_tree);
    }

    [[nodiscard]] auto RetrieveTreeFromForeignFile(const std::string& content,
                                                   const std::string& name,
                                                   bool executable)
        const noexcept -> expected<std::string, GitLookupError> {
        return stc_.ServeForeignFileTree(content, name, executable);
    }

    [[nodiscard]] auto ContentInRemoteCAS(
        std::string const& content) const noexcept -> bool {
        return stc_.ServeContent(content);
    }

    [[nodiscard]] auto TreeInRemoteCAS(
        std::string const& tree_id) const noexcept -> bool {
        return stc_.ServeTree(tree_id);
    }

    [[nodiscard]] auto CheckRootTree(std::string const& tree_id) const noexcept
        -> std::optional<bool> {
        return stc_.CheckRootTree(tree_id);
    }

    [[nodiscard]] auto GetTreeFromRemote(
        std::string const& tree_id) const noexcept -> bool {
        return stc_.GetRemoteTree(tree_id);
    }

    [[nodiscard]] auto ServeTargetVariables(std::string const& target_root_id,
                                            std::string const& target_file,
                                            std::string const& target)
        const noexcept -> std::optional<std::vector<std::string>> {
        return tc_.ServeTargetVariables(target_root_id, target_file, target);
    }

    [[nodiscard]] auto ServeTargetDescription(std::string const& target_root_id,
                                              std::string const& target_file,
                                              std::string const& target)
        const noexcept -> std::optional<ArtifactDigest> {
        return tc_.ServeTargetDescription(target_root_id, target_file, target);
    }

    [[nodiscard]] auto ServeTarget(const TargetCacheKey& key,
                                   const std::string& repo_key) const noexcept
        -> std::optional<serve_target_result_t> {
        return tc_.ServeTarget(key, repo_key);
    }

    [[nodiscard]] auto CheckServeRemoteExecution() const noexcept -> bool {
        return cc_.CheckServeRemoteExecution();
    }

    [[nodiscard]] auto IsCompatible() const noexcept -> std::optional<bool> {
        return cc_.IsCompatible();
    }

  private:
    // source tree service client
    SourceTreeClient const stc_;
    // target service client
    TargetClient const tc_;
    // configuration service client
    ConfigurationClient const cc_;
};

#endif  // BOOTSTRAP_BUILD_TOOL

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP
