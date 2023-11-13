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

#include "src/buildtool/serve_api/serve_service/serve_server_implementation.hpp"

#include <iostream>
#include <memory>

#include <sys/types.h>

#include "fmt/core.h"
#include "grpcpp/grpcpp.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/execution_api/execution_service/ac_server.hpp"
#include "src/buildtool/execution_api/execution_service/bytestream_server.hpp"
#include "src/buildtool/execution_api/execution_service/capabilities_server.hpp"
#include "src/buildtool/execution_api/execution_service/cas_server.hpp"
#include "src/buildtool/execution_api/execution_service/execution_server.hpp"
#include "src/buildtool/execution_api/execution_service/operations_server.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/serve_api/serve_service/configuration.hpp"
#include "src/buildtool/serve_api/serve_service/source_tree.hpp"
#include "src/buildtool/serve_api/serve_service/target.hpp"
#include "src/buildtool/storage/config.hpp"

namespace {
template <typename T>
auto TryWrite(std::string const& file, T const& content) noexcept -> bool {
    std::ofstream of{file};
    if (!of.good()) {
        Logger::Log(LogLevel::Error,
                    "Could not open {}. Make sure to have write permissions",
                    file);
        return false;
    }
    of << content;
    return true;
}
}  // namespace

auto ServeServerImpl::Run(bool with_execute) -> bool {
    // make sure the git root directory is properly initialized
    if (not FileSystemManager::CreateDirectory(StorageConfig::GitRoot())) {
        Logger::Log(LogLevel::Error,
                    "Could not create directory {}. Aborting",
                    StorageConfig::GitRoot().string());
        return false;
    }
    if (not GitRepo::InitAndOpen(StorageConfig::GitRoot(), true)) {
        Logger::Log(LogLevel::Error,
                    fmt::format("could not initialize bare git repository {}",
                                StorageConfig::GitRoot().string()));
        return false;
    }

    SourceTreeService sts{};
    TargetService ts{};
    ConfigurationService cs{};

    grpc::ServerBuilder builder;

    builder.RegisterService(&sts);
    builder.RegisterService(&ts);
    builder.RegisterService(&cs);

    // the user has not given any remote-execution endpoint
    // so we start a "just-execute instance" on the same process
    [[maybe_unused]] ExecutionServiceImpl es{};
    [[maybe_unused]] ActionCacheServiceImpl ac{};
    [[maybe_unused]] CASServiceImpl cas{};
    [[maybe_unused]] BytestreamServiceImpl b{};
    [[maybe_unused]] CapabilitiesServiceImpl cap{};
    [[maybe_unused]] OperarationsServiceImpl op{};
    if (with_execute) {
        builder.RegisterService(&es)
            .RegisterService(&ac)
            .RegisterService(&cas)
            .RegisterService(&b)
            .RegisterService(&cap)
            .RegisterService(&op);
    }

    std::shared_ptr<grpc::ServerCredentials> creds;
    if (Auth::GetAuthMethod() == AuthMethod::kTLS) {
        auto tls_opts = grpc::SslServerCredentialsOptions{};

        tls_opts.pem_root_certs = Auth::TLS::CACert();
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {
            Auth::TLS::ServerKey(), Auth::TLS::ServerCert()};

        tls_opts.pem_key_cert_pairs.emplace_back(keycert);

        creds = grpc::SslServerCredentials(tls_opts);
    }
    else {
        creds = grpc::InsecureServerCredentials();
    }

    builder.AddListeningPort(
        fmt::format("{}:{}", interface_, port_), creds, &port_);

    auto server = builder.BuildAndStart();
    if (!server) {
        Logger::Log(LogLevel::Error, "Could not start serve service");
        return false;
    }

    if (with_execute and !RemoteExecutionConfig::SetRemoteAddress(
                             fmt::format("{}:{}", interface_, port_))) {
        Logger::Log(LogLevel::Error,
                    "Internal error: cannot set the remote address");
        return false;
    }

    auto pid = getpid();

    nlohmann::json const& info = {
        {"interface", interface_}, {"port", port_}, {"pid", pid}};

    if (!pid_file_.empty()) {
        if (!TryWrite(pid_file_, pid)) {
            server->Shutdown();
            return false;
        }
    }

    auto const& info_str = nlohmann::to_string(info);
    Logger::Log(LogLevel::Info,
                fmt::format("{}serve{} service{} started: {}",
                            Compatibility::IsCompatible() ? "compatible " : "",
                            with_execute ? " and execute" : "",
                            with_execute ? "s" : "",
                            info_str));

    if (!info_file_.empty()) {
        if (!TryWrite(info_file_, info_str)) {
            server->Shutdown();
            return false;
        }
    }
    server->Wait();
    return true;
}

[[nodiscard]] auto ServeServerImpl::SetInfoFile(std::string const& x) noexcept
    -> bool {
    Instance().info_file_ = x;
    return true;
}

[[nodiscard]] auto ServeServerImpl::SetPidFile(std::string const& x) noexcept
    -> bool {
    Instance().pid_file_ = x;
    return true;
}

[[nodiscard]] auto ServeServerImpl::SetPort(int const x) noexcept -> bool {
    auto port_num = ParsePort(x);
    if (!port_num) {
        return false;
    }
    Instance().port_ = static_cast<int>(*port_num);
    return true;
}
