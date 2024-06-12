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
#include "src/buildtool/logging/log_level.hpp"

namespace {

auto StringToArchiveType(std::string const& type) noexcept
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
                                       bool sync_tree) const noexcept
    -> result_t {
    justbuild::just_serve::ServeCommitTreeRequest request{};
    request.set_commit(commit_id);
    request.set_subdir(subdir);
    request.set_sync_tree(sync_tree);

    grpc::ClientContext context;
    justbuild::just_serve::ServeCommitTreeResponse response;
    grpc::Status status = stub_->ServeCommitTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return true;  // fatal failure
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeCommitTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeCommitTree response returned with {}",
                     static_cast<int>(response.status()));
        return /*fatal = */ (
            response.status() !=
            ::justbuild::just_serve::ServeCommitTreeResponse::NOT_FOUND);
    }
    return response.tree();  // success
}

auto SourceTreeClient::ServeArchiveTree(
    std::string const& content,
    std::string const& archive_type,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& resolve_symlinks,
    bool sync_tree) const noexcept -> result_t {
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
        return true;  // fatal failure
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeArchiveTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeArchiveTree response returned with {}",
                     static_cast<int>(response.status()));
        return /*fatal = */ (
            response.status() !=
            ::justbuild::just_serve::ServeArchiveTreeResponse::NOT_FOUND);
    }
    return response.tree();  // success
}

auto SourceTreeClient::ServeDistdirTree(
    std::shared_ptr<std::unordered_map<std::string, std::string>> const&
        distfiles,
    bool sync_tree) const noexcept -> result_t {
    justbuild::just_serve::ServeDistdirTreeRequest request{};
    for (auto const& [k, v] : *distfiles) {
        auto* distfile = request.add_distfiles();
        distfile->set_name(k);
        distfile->set_content(v);
        distfile->set_executable(false);
    }
    request.set_sync_tree(sync_tree);

    grpc::ClientContext context;
    justbuild::just_serve::ServeDistdirTreeResponse response;
    grpc::Status status = stub_->ServeDistdirTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return true;  // fatal failure
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeDistdirTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeDistdirTree response returned with {}",
                     static_cast<int>(response.status()));
        return /*fatal = */ (
            response.status() !=
            ::justbuild::just_serve::ServeDistdirTreeResponse::NOT_FOUND);
    }
    return response.tree();  // success
}

auto SourceTreeClient::ServeForeignFileTree(const std::string& content,
                                            const std::string& name,
                                            bool executable) const noexcept
    -> result_t {
    justbuild::just_serve::ServeDistdirTreeRequest request{};
    auto* distfile = request.add_distfiles();
    distfile->set_name(name);
    distfile->set_content(content);
    distfile->set_executable(executable);

    grpc::ClientContext context;
    justbuild::just_serve::ServeDistdirTreeResponse response;
    grpc::Status status = stub_->ServeDistdirTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return true;  // fatal failure
    }
    if (response.status() !=
        ::justbuild::just_serve::ServeDistdirTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeDistdirTree called for foreign file response "
                     "returned with {}",
                     static_cast<int>(response.status()));
        return /*fatal = */ (
            response.status() !=
            ::justbuild::just_serve::ServeDistdirTreeResponse::NOT_FOUND);
    }
    return response.tree();  // success
}

auto SourceTreeClient::ServeContent(std::string const& content) const noexcept
    -> bool {
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

auto SourceTreeClient::ServeTree(std::string const& tree_id) const noexcept
    -> bool {
    justbuild::just_serve::ServeTreeRequest request{};
    request.set_tree(tree_id);

    grpc::ClientContext context;
    justbuild::just_serve::ServeTreeResponse response;
    grpc::Status status = stub_->ServeTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return false;
    }
    if (response.status() != ::justbuild::just_serve::ServeTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "ServeTree response returned with {}",
                     static_cast<int>(response.status()));
        return false;
    }
    return true;
}

auto SourceTreeClient::CheckRootTree(std::string const& tree_id) const noexcept
    -> std::optional<bool> {
    justbuild::just_serve::CheckRootTreeRequest request{};
    request.set_tree(tree_id);

    grpc::ClientContext context;
    justbuild::just_serve::CheckRootTreeResponse response;
    grpc::Status status = stub_->CheckRootTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return std::nullopt;
    }
    if (response.status() !=
        ::justbuild::just_serve::CheckRootTreeResponse::OK) {
        if (response.status() !=
            ::justbuild::just_serve::CheckRootTreeResponse::NOT_FOUND) {
            logger_.Emit(LogLevel::Debug,
                         "CheckRootTree response returned with {}",
                         static_cast<int>(response.status()));
            return std::nullopt;  // tree lookup failed
        }
        return false;  // tree not found
    }
    return true;  // tree found
}

auto SourceTreeClient::GetRemoteTree(std::string const& tree_id) const noexcept
    -> bool {
    justbuild::just_serve::GetRemoteTreeRequest request{};
    request.set_tree(tree_id);

    grpc::ClientContext context;
    justbuild::just_serve::GetRemoteTreeResponse response;
    grpc::Status status = stub_->GetRemoteTree(&context, request, &response);

    if (not status.ok()) {
        LogStatus(&logger_, LogLevel::Debug, status);
        return false;
    }
    if (response.status() !=
        ::justbuild::just_serve::GetRemoteTreeResponse::OK) {
        logger_.Emit(LogLevel::Debug,
                     "GetRemoteTree response returned with {}",
                     static_cast<int>(response.status()));
        return false;  // retrieving tree or import-to-git failed
    }
    // success!
    return true;
}
