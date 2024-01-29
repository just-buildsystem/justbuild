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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_SERVE_SERVICE_SOURCE_TREE_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_SERVE_SERVICE_SOURCE_TREE_HPP

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "justbuild/just_serve/just_serve.grpc.pb.h"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/execution_api/common/create_execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/symlinks_map/pragma_special.hpp"
#include "src/buildtool/file_system/symlinks_map/resolve_symlinks_map.hpp"
#include "src/buildtool/logging/logger.hpp"

class SourceTreeService final
    : public justbuild::just_serve::SourceTree::Service {

  public:
    using ServeCommitTreeResponse =
        ::justbuild::just_serve::ServeCommitTreeResponse;
    using ServeArchiveTreeResponse =
        ::justbuild::just_serve::ServeArchiveTreeResponse;
    using ServeDistdirTreeResponse =
        ::justbuild::just_serve::ServeDistdirTreeResponse;

    using ServeContentResponse = ::justbuild::just_serve::ServeContentResponse;
    using ServeTreeResponse = ::justbuild::just_serve::ServeTreeResponse;

    using CheckRootTreeResponse =
        ::justbuild::just_serve::CheckRootTreeResponse;
    using GetRemoteTreeResponse =
        ::justbuild::just_serve::GetRemoteTreeResponse;

    // Retrieve the Git-subtree identifier from a given Git commit.
    //
    // There are no method-specific errors.
    auto ServeCommitTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::ServeCommitTreeRequest* request,
        ServeCommitTreeResponse* response) -> ::grpc::Status override;

    // Retrieve the Git-subtree identifier for the tree obtained
    // by unpacking an archive with a given blob identifier.
    //
    // There are no method-specific errors.
    auto ServeArchiveTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::ServeArchiveTreeRequest* request,
        ServeArchiveTreeResponse* response) -> ::grpc::Status override;

    // Compute the Git-tree identifier for the tree containing the content
    // blobs of a list of distfiles. The implementation must only return the
    // tree identifier if ALL content blobs are known.
    //
    // There are no method-specific errors.
    auto ServeDistdirTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::ServeDistdirTreeRequest* request,
        ServeDistdirTreeResponse* response) -> ::grpc::Status override;

    // Make the blob identifier of an archive content available in
    // remote CAS, if blob is known.
    //
    // There are no method-specific errors.
    auto ServeContent(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::ServeContentRequest* request,
        ServeContentResponse* response) -> ::grpc::Status override;

    // Make a given tree identifier available in remote CAS,
    // if tree is known.
    //
    // There are no method-specific errors.
    auto ServeTree(::grpc::ServerContext* context,
                   const ::justbuild::just_serve::ServeTreeRequest* request,
                   ServeTreeResponse* response) -> ::grpc::Status override;

    // Checks if a Git-tree is locally known and, if found, makes it available
    // in a location where this serve instance can build against.
    // The implementation should not interrogate the associated remote-execution
    // endpoint at any point during the completion of this request.
    //
    // There are no method-specific errors.
    auto CheckRootTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::CheckRootTreeRequest* request,
        CheckRootTreeResponse* response) -> ::grpc::Status override;

    // Retrieves a given Git-tree from the CAS of the associated
    // remote-execution endpoint and makes it available in a location where this
    // serve instance can build against.
    //
    // There are no method-specific errors.
    auto GetRemoteTree(
        ::grpc::ServerContext* context,
        const ::justbuild::just_serve::GetRemoteTreeRequest* request,
        GetRemoteTreeResponse* response) -> ::grpc::Status override;

  private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<Logger> logger_{std::make_shared<Logger>("serve-service")};

    // remote execution endpoint
    gsl::not_null<IExecutionApi::Ptr> const remote_api_{
        CreateExecutionApi(RemoteExecutionConfig::RemoteAddress(),
                           std::nullopt,
                           "serve-remote-execution")};
    // local api
    gsl::not_null<IExecutionApi::Ptr> const local_api_{
        CreateExecutionApi(std::nullopt)};
    // symlinks resolver map
    ResolveSymlinksMap resolve_symlinks_map_{CreateResolveSymlinksMap()};

    /// \brief Check if commit exists and tries to get the subtree if found.
    /// \returns An error + data union, where at index 0 is a failure type flag
    /// and at index 1 is the subtree hash on success. The failure flag is false
    /// if commit was not found and true if failed otherwise.
    [[nodiscard]] static auto GetSubtreeFromCommit(
        std::filesystem::path const& repo_path,
        std::string const& commit,
        std::string const& subdir,
        std::shared_ptr<Logger> const& logger)
        -> std::variant<bool, std::string>;

    /// \brief Check if tree exists and tries to get the subtree if found.
    /// \returns An error + data union, where at index 0 is a failure type flag
    /// and at index 1 is the subtree hash on success. The failure flag is true
    /// if repository could not be opened (i.e., fatal failure), false if the
    /// check for the subtree itself failed.
    [[nodiscard]] static auto GetSubtreeFromTree(
        std::filesystem::path const& repo_path,
        std::string const& tree_id,
        std::string const& subdir,
        std::shared_ptr<Logger> const& logger)
        -> std::variant<bool, std::string>;

    /// \brief Tries to retrieve the blob from a repository.
    /// \returns An error + data union, where at index 0 is a failure type flag
    /// and at index 1 is the subtree hash on success. The failure flag is false
    /// if blob was not found and true if failed otherwise.
    [[nodiscard]] static auto GetBlobFromRepo(
        std::filesystem::path const& repo_path,
        std::string const& blob_id,
        std::shared_ptr<Logger> const& logger)
        -> std::variant<bool, std::string>;

    [[nodiscard]] auto SyncArchive(std::string const& tree_id,
                                   std::filesystem::path const& repo_path,
                                   bool sync_tree,
                                   ServeArchiveTreeResponse* response)
        -> ::grpc::Status;

    [[nodiscard]] auto ResolveContentTree(
        std::string const& tree_id,
        std::filesystem::path const& repo_path,
        std::optional<PragmaSpecial> const& resolve_special,
        bool sync_tree,
        ServeArchiveTreeResponse* response) -> ::grpc::Status;

    /// \brief Common import-to-git utility, used by both archives and distdirs.
    /// \returns An error + data union, where at index 0 is the error message on
    /// failure and at index 1 is the root tree id of the committed directory on
    /// success.
    [[nodiscard]] auto CommonImportToGit(std::filesystem::path const& root_path,
                                         std::string const& commit_message)
        -> std::variant<std::string, std::string>;

    [[nodiscard]] auto ArchiveImportToGit(
        std::filesystem::path const& unpack_path,
        std::filesystem::path const& archive_tree_id_file,
        std::string const& content,
        std::string const& archive_type,
        std::string const& subdir,
        std::optional<PragmaSpecial> const& resolve_special,
        bool sync_tree,
        ServeArchiveTreeResponse* response) -> ::grpc::Status;

    /// \brief Checks if a given tree is a repository.
    /// \returns A status of tree presence, or nullopt if non-check-related
    /// failure.
    [[nodiscard]] static auto IsTreeInRepo(
        std::string const& tree_id,
        std::filesystem::path const& repo_path,
        std::shared_ptr<Logger> const& logger) -> std::optional<bool>;

    [[nodiscard]] auto DistdirImportToGit(
        std::string const& tree_id,
        std::string const& content_id,
        std::unordered_map<std::string, std::string> const& content_list,
        bool sync_tree,
        ServeDistdirTreeResponse* response) -> ::grpc::Status;
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_SERVE_SERVICE_SOURCE_TREE_HPP
