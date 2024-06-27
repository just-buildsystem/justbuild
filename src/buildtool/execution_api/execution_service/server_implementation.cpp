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

#include "src/buildtool/execution_api/execution_service/server_implementation.hpp"

#include <iostream>
#include <memory>

#ifdef __unix__
#include <sys/types.h>
#else
#error "Non-unix is not supported yet"
#endif

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
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

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

auto ServerImpl::Run(ApiBundle const& apis) -> bool {
    ExecutionServiceImpl es{&*apis.local};
    ActionCacheServiceImpl ac{};
    CASServiceImpl cas{};
    BytestreamServiceImpl b{};
    CapabilitiesServiceImpl cap{};
    OperarationsServiceImpl op{};

    grpc::ServerBuilder builder;

    builder.RegisterService(&es)
        .RegisterService(&ac)
        .RegisterService(&cas)
        .RegisterService(&b)
        .RegisterService(&cap)
        .RegisterService(&op);

    std::shared_ptr<grpc::ServerCredentials> creds;
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auto tls_opts = grpc::SslServerCredentialsOptions{};

        tls_opts.pem_root_certs = Auth::TLS::Instance().CACert();
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {
            Auth::TLS::Instance().ServerKey(),
            Auth::TLS::Instance().ServerCert()};

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
        Logger::Log(LogLevel::Error, "Could not start execution service");
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
                fmt::format("{}execution service started: {}",
                            Compatibility::IsCompatible() ? "compatible " : "",
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

[[nodiscard]] auto ServerImpl::SetInfoFile(std::string const& x) noexcept
    -> bool {
    Instance().info_file_ = x;
    return true;
}

[[nodiscard]] auto ServerImpl::SetPidFile(std::string const& x) noexcept
    -> bool {
    Instance().pid_file_ = x;
    return true;
}

[[nodiscard]] auto ServerImpl::SetPort(int const x) noexcept -> bool {
    auto port_num = ParsePort(x);
    if (!port_num) {
        return false;
    }
    Instance().port_ = static_cast<int>(*port_num);
    return true;
}
