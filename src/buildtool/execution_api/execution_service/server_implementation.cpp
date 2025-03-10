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

#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/execution_service/ac_server.hpp"
#include "src/buildtool/execution_api/execution_service/bytestream_server.hpp"
#include "src/buildtool/execution_api/execution_service/capabilities_server.hpp"
#include "src/buildtool/execution_api/execution_service/cas_server.hpp"
#include "src/buildtool/execution_api/execution_service/execution_server.hpp"
#include "src/buildtool/execution_api/execution_service/operations_server.hpp"
#include "src/buildtool/file_system/atomic.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/type_safe_arithmetic.hpp"

auto ServerImpl::Create(std::optional<std::string> interface,
                        std::optional<int> port,
                        std::optional<std::string> info_file,
                        std::optional<std::string> pid_file) noexcept
    -> std::optional<ServerImpl> {
    ServerImpl server;
    if (interface) {
        server.interface_ = std::move(*interface);
    }
    if (port) {
        auto parsed_port = ParsePort(*port);
        if (parsed_port) {
            server.port_ = static_cast<int>(*parsed_port);
        }
        else {
            Logger::Log(LogLevel::Error, "Invalid port '{}'", *port);
            return std::nullopt;
        }
    }
    if (info_file) {
        server.info_file_ = std::move(*info_file);
    }
    if (pid_file) {
        server.pid_file_ = std::move(*pid_file);
    }
    return server;
}

auto ServerImpl::Run(gsl::not_null<LocalContext const*> const& local_context,
                     gsl::not_null<RemoteContext const*> const& remote_context,
                     ApiBundle const& apis,
                     std::optional<std::uint8_t> op_exponent) -> bool {
    auto const hash_type =
        local_context->storage_config->hash_function.GetType();
    ExecutionServiceImpl es{local_context, &*apis.local, op_exponent};
    ActionCacheServiceImpl ac{local_context};
    CASServiceImpl cas{local_context};
    BytestreamServiceImpl b{local_context};
    CapabilitiesServiceImpl cap{hash_type};
    OperationsServiceImpl op{&es.GetOpCache()};

    grpc::ServerBuilder builder;

    builder.RegisterService(&es)
        .RegisterService(&ac)
        .RegisterService(&cas)
        .RegisterService(&b)
        .RegisterService(&cap)
        .RegisterService(&op);

    // check authentication credentials; currently only TLS/SSL is supported
    std::shared_ptr<grpc::ServerCredentials> creds;
    if (const auto* tls_auth =
            std::get_if<Auth::TLS>(&remote_context->auth->method);
        tls_auth != nullptr) {
        auto tls_opts = grpc::SslServerCredentialsOptions{};

        tls_opts.pem_root_certs = tls_auth->ca_cert;
        grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {
            tls_auth->server_key, tls_auth->server_cert};

        tls_opts.pem_key_cert_pairs.emplace_back(keycert);

        creds = grpc::SslServerCredentials(tls_opts);
    }
    else {
        creds = grpc::InsecureServerCredentials();
    }

    builder.AddListeningPort(
        fmt::format("{}:{}", interface_, port_), creds, &port_);

    auto server = builder.BuildAndStart();
    if (not server) {
        Logger::Log(LogLevel::Error, "Could not start execution service");
        return false;
    }

    auto pid = getpid();

    nlohmann::json const& info = {
        {"interface", interface_}, {"port", port_}, {"pid", pid}};

    if (not pid_file_.empty()) {
        if (not FileSystemAtomic::WriteFile(pid_file_,
                                            fmt::format("{}", pid))) {
            server->Shutdown();
            return false;
        }
    }

    auto const info_str = nlohmann::to_string(info);
    Logger::Log(LogLevel::Info,
                "{}execution service started: {}",
                ProtocolTraits::IsNative(hash_type) ? "" : "compatible ",
                info_str);

    if (not info_file_.empty()) {
        if (not FileSystemAtomic::WriteFile(info_file_, info_str)) {
            server->Shutdown();
            return false;
        }
    }

    server->Wait();
    return true;
}
