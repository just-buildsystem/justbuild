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

#include "src/buildtool/serve_api/remote/source_tree_client.hpp"

#include "src/buildtool/common/remote/client_common.hpp"

namespace {

auto StringToArchiveType(std::string const& type)
    -> justbuild::just_serve::ServeArchiveTreeRequest_ArchiveType {
    using ServeArchiveType =
        justbuild::just_serve::ServeArchiveTreeRequest_ArchiveType;
    return type == "zip"
               ? ServeArchiveType::ServeArchiveTreeRequest_ArchiveType_ZIP
               : ServeArchiveType::ServeArchiveTreeRequest_ArchiveType_TAR;
}

auto PragmaSpecialToSymlinksResolve(
    std::optional<PragmaSpecial> const& resolve_symlinks)
    -> justbuild::just_serve::ServeArchiveTreeRequest_SymlinksResolve {
    using ServeSymlinksResolve =
        justbuild::just_serve::ServeArchiveTreeRequest_SymlinksResolve;
    if (not resolve_symlinks) {
        return ServeSymlinksResolve::
            ServeArchiveTreeRequest_SymlinksResolve_NONE;
    }
    switch (resolve_symlinks.value()) {
        case PragmaSpecial::Ignore: {
            return ServeSymlinksResolve::
                ServeArchiveTreeRequest_SymlinksResolve_IGNORE;
        }
        case PragmaSpecial::ResolvePartially: {
            return ServeSymlinksResolve::
                ServeArchiveTreeRequest_SymlinksResolve_PARTIAL;
        }
        case PragmaSpecial::ResolveCompletely: {
            return ServeSymlinksResolve::
                ServeArchiveTreeRequest_SymlinksResolve_COMPLETE;
        }
    }
    // default return, to avoid [-Werror=return-type] error
    return ServeSymlinksResolve::ServeArchiveTreeRequest_SymlinksResolve_NONE;
}

}  // namespace

SourceTreeClient::SourceTreeClient(std::string const& server,
                                   Port port) noexcept {
    stub_ = justbuild::just_serve::SourceTree::NewStub(
        CreateChannelWithCredentials(server, port));
}

auto SourceTreeClient::ServeCommitTree(std::string const& commit_id,
                                       std::string const& subdir,
                                       bool sync_tree)
    -> std::optional<std::string> {
    justbuild::just_serve::ServeCommitTreeRequest request{};
    request.set_commit(commit_id);
    request.set_subdir(subdir);
    request.set_sync_tree(sync_tree);

    grpc::ClientContext context;
    justbuild::just_serve::ServeCommitTreeResponse response;
    grpc::Status status = stub_->ServeCommitTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return std::nullopt;
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeCommitTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeCommitTree response returned with {}",
                     static_cast<int>(response.status()));
        return std::nullopt;
    }
    return response.tree();
}

auto SourceTreeClient::ServeArchiveTree(
    std::string const& content,
    std::string const& archive_type,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& resolve_symlinks,
    bool sync_tree) -> std::optional<std::string> {
    justbuild::just_serve::ServeArchiveTreeRequest request{};
    request.set_content(content);
    request.set_archive_type(StringToArchiveType(archive_type));
    request.set_subdir(subdir);
    request.set_resolve_symlinks(
        PragmaSpecialToSymlinksResolve(resolve_symlinks));
    request.set_sync_tree(sync_tree);

    grpc::ClientContext context;
    justbuild::just_serve::ServeArchiveTreeResponse response;
    grpc::Status status = stub_->ServeArchiveTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return std::nullopt;
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeArchiveTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeArchiveTree response returned with {}",
                     static_cast<int>(response.status()));
        return std::nullopt;
    }
    return response.tree();
}

auto SourceTreeClient::ServeContent(std::string const& content) -> bool {
    justbuild::just_serve::ServeContentRequest request{};
    request.set_content(content);

    grpc::ClientContext context;
    justbuild::just_serve::ServeContentResponse response;
    grpc::Status status = stub_->ServeContent(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return false;
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeContentResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeContent response returned with {}",
                     static_cast<int>(response.status()));
        return false;
    }
    return true;
}
