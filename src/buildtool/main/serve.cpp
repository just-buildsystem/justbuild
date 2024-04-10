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

#include "src/buildtool/main/serve.hpp"

#include <chrono>
#include <cstddef>

#ifndef BOOTSTRAP_BUILD_TOOL

#include <fstream>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/remote/retry_parameters.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/exit_codes.hpp"

[[nodiscard]] auto ParseRetryCliOptions(Configuration const& config) noexcept
    -> bool {
    auto max_attempts = config["max-attempts"];
    if (max_attempts.IsNotNull()) {
        if (!max_attempts->IsNumber()) {
            Logger::Log(
                LogLevel::Error,
                "Invalid value for \"max-attempts\" {}. It must be a number.",
                max_attempts->ToString());
            return false;
        }
        if (!Retry::SetMaxAttempts(max_attempts->Number())) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for \"max-attempts\" {}.",
                        max_attempts->Number());
            return false;
        }
    }
    auto initial_backoff = config["initial-backoff-seconds"];
    if (initial_backoff.IsNotNull()) {
        if (!initial_backoff->IsNumber()) {
            Logger::Log(LogLevel::Error,
                        "Invalid value \"initial-backoff-seconds\" {}. It must "
                        "be a number.",
                        initial_backoff->ToString());
            return false;
        }
        if (!Retry::SetMaxAttempts(initial_backoff->Number())) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for \"initial-backoff-seconds\" {}.",
                        initial_backoff->Number());
            return false;
        }
    }
    auto max_backoff = config["max-backoff-seconds"];
    if (max_backoff.IsNotNull()) {
        if (!max_backoff->IsNumber()) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for \"max-backoff-seconds\" {}. It must "
                        "be a number.",
                        max_backoff->ToString());
            return false;
        }
        if (!Retry::SetMaxAttempts(max_backoff->Number())) {
            Logger::Log(LogLevel::Error,
                        "Invalid value for \"max-backoff-seconds\" {}.",
                        max_backoff->Number());
            return false;
        }
    }
    return true;
}

void ReadJustServeConfig(gsl::not_null<CommandLineArguments*> const& clargs) {
    Configuration serve_config{};
    auto serve_path = clargs->serve.config;
    if (not FileSystemManager::ResolveSymlinks(&serve_path)) {
        return;
    }
    if (FileSystemManager::IsFile(serve_path)) {
        // json::parse may throw
        try {
            std::ifstream fs(clargs->serve.config);
            auto map = Expression::FromJson(nlohmann::json::parse(fs));
            if (not map->IsMap()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nExpected an "
                            "object but found:\n{}",
                            clargs->serve.config.string(),
                            map->ToString());
                std::exit(kExitFailure);
            }
            serve_config = Configuration{map};
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error,
                        "Parsing serve service config file {} as JSON failed "
                        "with error:\n{}",
                        clargs->serve.config.string(),
                        e.what());
            std::exit(kExitFailure);
        }
    }
    else {
        Logger::Log(LogLevel::Error,
                    "Cannot read serve service config file {}",
                    clargs->serve.config.string());
        std::exit(kExitFailure);
    }
    // read local build root
    auto local_root = serve_config["local build root"];
    if (local_root.IsNotNull()) {
        if (not local_root->IsString()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"local build root\" has to be a string, but found {}",
                        clargs->serve.config.string(),
                        local_root->ToString());
            std::exit(kExitFailure);
        }
        clargs->endpoint.local_root = local_root->String();
    }
    // read paths of additional lookup repositories
    auto repositories = serve_config["repositories"];
    if (repositories.IsNotNull()) {
        if (not repositories->IsList()) {
            Logger::Log(
                LogLevel::Error,
                "In serve service config file {}:\nValue for key "
                "\"repositories\" has to be a list of strings, but found {}",
                clargs->serve.config.string(),
                repositories->ToString());
            std::exit(kExitFailure);
        }
        auto const& repos_list = repositories->List();
        clargs->serve.repositories.reserve(clargs->serve.repositories.size() +
                                           repos_list.size());
        for (auto const& repo : repos_list) {
            if (not repo->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nExpected each "
                            "repository path to be a string, but found {}",
                            clargs->serve.config.string(),
                            repo->ToString());
                std::exit(kExitFailure);
            }
            clargs->serve.repositories.emplace_back(repo->String());
        }
    }
    // read logging arguments
    auto logging = serve_config["logging"];
    if (logging.IsNotNull()) {
        if (not logging->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"logging\" has to be a map, but found {}",
                        clargs->serve.config.string(),
                        logging->ToString());
            std::exit(kExitFailure);
        }
        // read in first the append flag
        auto append = logging->Get("append", Expression::none_t{});
        if (append.IsNotNull()) {
            if (not append->IsBool()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for logging key "
                    "\"append\" has to be a flag, but found {}",
                    clargs->serve.config.string(),
                    append->ToString());
                std::exit(kExitFailure);
            }
            clargs->log.log_append = append->Bool();
        }
        // read in the plain flag
        auto plain = logging->Get("plain", Expression::none_t{});
        if (plain.IsNotNull()) {
            if (not plain->IsBool()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for logging key "
                    "\"plain\" has to be a flag, but found {}",
                    clargs->serve.config.string(),
                    plain->ToString());
                std::exit(kExitFailure);
            }
            clargs->log.plain_log = plain->Bool();
        }
        // read in files field
        auto files = logging->Get("files", Expression::none_t{});
        if (files.IsNotNull()) {
            if (not files->IsList()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for logging key "
                    "\"files\" has to be a list, but found {}",
                    clargs->serve.config.string(),
                    files->ToString());
                std::exit(kExitFailure);
            }
            auto const& files_list = files->List();
            clargs->log.log_files.reserve(clargs->log.log_files.size() +
                                          files_list.size());
            for (auto const& file : files_list) {
                if (not file->IsString()) {
                    Logger::Log(
                        LogLevel::Error,
                        "In serve service config file {}:\nExpected each log "
                        "file path to be a string, but found {}",
                        clargs->serve.config.string(),
                        file->ToString());
                    std::exit(kExitFailure);
                }
                clargs->log.log_files.emplace_back(file->String());
            }
        }
        // read in limit field
        auto limit = logging->Get("limit", Expression::none_t{});
        if (limit.IsNotNull()) {
            if (not limit->IsNumber()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for logging key "
                    "\"limit\" has to be numeric, but found {}",
                    clargs->serve.config.string(),
                    limit->ToString());
                std::exit(kExitFailure);
            }
            clargs->log.log_limit = ToLogLevel(limit->Number());
        }
    }
    // read client TLS authentication arguments
    auto auth_args = serve_config["authentication"];
    if (auth_args.IsNotNull()) {
        if (not auth_args->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"authentication\" has to be a map, but found {}",
                        clargs->serve.config.string(),
                        auth_args->ToString());
            std::exit(kExitFailure);
        }
        // read the TLS CA certificate
        auto cacert = auth_args->Get("ca cert", Expression::none_t{});
        if (cacert.IsNotNull()) {
            if (not cacert->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "authentication key \"ca cert\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            cacert->ToString());
                std::exit(kExitFailure);
            }
            clargs->auth.tls_ca_cert = cacert->String();
        }
        // read the TLS client certificate
        auto client_cert = auth_args->Get("client cert", Expression::none_t{});
        if (client_cert.IsNotNull()) {
            if (not client_cert->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "authentication key \"client cert\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            client_cert->ToString());
                std::exit(kExitFailure);
            }
            clargs->cauth.tls_client_cert = client_cert->String();
        }
        // read the TLS client key
        auto client_key = auth_args->Get("client key", Expression::none_t{});
        if (client_key.IsNotNull()) {
            if (not client_key->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "authentication key \"client key\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            client_key->ToString());
                std::exit(kExitFailure);
            }
            clargs->cauth.tls_client_key = client_key->String();
        }
    }
    // read remote service arguments
    auto remote_service = serve_config["remote service"];
    if (remote_service.IsNotNull()) {
        if (not remote_service->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"remote service\" has to be a map, but found {}",
                        clargs->serve.config.string(),
                        remote_service->ToString());
            std::exit(kExitFailure);
        }
        // read the interface
        auto interface = remote_service->Get("interface", Expression::none_t{});
        if (interface.IsNotNull()) {
            if (not interface->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "remote service key \"interface\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            interface->ToString());
                std::exit(kExitFailure);
            }
            clargs->service.interface = interface->String();
        }
        // read the port
        auto port = remote_service->Get("port", Expression::none_t{});
        if (port.IsNotNull()) {
            if (not port->IsNumber()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for remote "
                    "service key \"port\" has to be numeric, but found {}",
                    clargs->serve.config.string(),
                    port->ToString());
                std::exit(kExitFailure);
            }
            double val{};
            if (std::modf(port->Number(), &val) != 0.0) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for remote "
                    "service key \"port\" has to be an integer, but found {}",
                    interface->ToString());
                std::exit(kExitFailure);
            }
            // we are sure now that the port is an integer
            clargs->service.port = std::nearbyint(val);
        }
        // read the pid file
        auto pid_file = remote_service->Get("pid file", Expression::none_t{});
        if (pid_file.IsNotNull()) {
            if (not pid_file->IsString()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for remote "
                    "service key \"pid file\" has to be a string, but found {}",
                    clargs->serve.config.string(),
                    pid_file->ToString());
                std::exit(kExitFailure);
            }
            clargs->service.pid_file = pid_file->String();
        }
        // read the info file
        auto info_file = remote_service->Get("info file", Expression::none_t{});
        if (info_file.IsNotNull()) {
            if (not info_file->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "remote service key \"info file\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            info_file->ToString());
                std::exit(kExitFailure);
            }
            clargs->service.info_file = info_file->String();
        }
        // read the TLS server certificate
        auto server_cert =
            remote_service->Get("server cert", Expression::none_t{});
        if (server_cert.IsNotNull()) {
            if (not server_cert->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "remote service key \"server cert\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            server_cert->ToString());
                std::exit(kExitFailure);
            }
            clargs->sauth.tls_server_cert = server_cert->String();
        }
        // read the TLS server key
        auto server_key =
            remote_service->Get("server key", Expression::none_t{});
        if (server_key.IsNotNull()) {
            if (not server_key->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "remote service key \"server key\" has to be a "
                            "string, but found {}",
                            clargs->serve.config.string(),
                            server_key->ToString());
                std::exit(kExitFailure);
            }
            clargs->sauth.tls_server_key = server_key->String();
        }
    }
    // read execution endpoint arguments
    auto exec_endpoint = serve_config["execution endpoint"];
    if (exec_endpoint.IsNotNull()) {
        if (not exec_endpoint->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nvalue for key "
                        "\"execution endpoint\" has to be a map, but found {}",
                        clargs->serve.config.string(),
                        exec_endpoint->ToString());
            std::exit(kExitFailure);
        }
        // read the compatible flag
        auto compatible =
            exec_endpoint->Get("compatible", Expression::none_t{});
        if (compatible.IsNotNull()) {
            if (not compatible->IsBool()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for "
                            "execution endpoint key \"compatible\" has to be a "
                            "flag, but found {}",
                            clargs->serve.config.string(),
                            compatible->ToString());
                std::exit(kExitFailure);
            }
            // compatibility is set immediately if flag is true
            if (compatible->Bool()) {
                Compatibility::SetCompatible();
            }
        }
        // read the address
        auto address = exec_endpoint->Get("address", Expression::none_t{});
        if (address.IsNotNull()) {
            if (not address->IsString()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for execution "
                    "endpoint key \"address\" has to be a string, but found {}",
                    clargs->serve.config.string(),
                    address->ToString());
                std::exit(kExitFailure);
            }
            clargs->endpoint.remote_execution_address = address->String();
        }
        if (!ParseRetryCliOptions(serve_config)) {
            std::exit(kExitFailure);
        }
    }
    // read jobs value
    auto jobs = serve_config["jobs"];
    if (jobs.IsNotNull()) {
        if (not jobs->IsNumber()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"jobs\" has to be a number, but found {}",
                        clargs->serve.config.string(),
                        jobs->ToString());
            std::exit(kExitFailure);
        }
        clargs->common.jobs = jobs->Number();
    }
    // read build options
    auto build_args = serve_config["build"];
    if (build_args.IsNotNull()) {
        if (not build_args->IsMap()) {
            Logger::Log(LogLevel::Error,
                        "In serve service config file {}:\nValue for key "
                        "\"build\" has to be a map, but found {}",
                        clargs->serve.config.string(),
                        build_args->ToString());
            std::exit(kExitFailure);
        }
        // read the build jobs
        auto build_jobs = build_args->Get("build jobs", Expression::none_t{});
        if (build_jobs.IsNotNull()) {
            if (not build_jobs->IsNumber()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for build key "
                    "\"build jobs\" has to be a number, but found {}",
                    clargs->serve.config.string(),
                    build_jobs->ToString());
                std::exit(kExitFailure);
            }
            clargs->build.build_jobs = build_jobs->Number();
        }
        else {
            clargs->build.build_jobs = clargs->common.jobs;
        }
        // read action timeout
        auto timeout = build_args->Get("action timeout", Expression::none_t{});
        if (timeout.IsNotNull()) {
            if (not timeout->IsNumber()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for build key "
                    "\"action timeout\" has to be a number, but found {}",
                    clargs->serve.config.string(),
                    timeout->ToString());
                std::exit(kExitFailure);
            }
            clargs->build.timeout =
                std::size_t(timeout->Number()) * std::chrono::seconds{1};
        }
        auto strategy = build_args->Get("target-cache write strategy",
                                        Expression::none_t{});
        if (strategy.IsNotNull()) {
            if (not strategy->IsString()) {
                Logger::Log(LogLevel::Error,
                            "In serve service config file {}:\nValue for build "
                            "key \"target-cache write strategy\" has "
                            "to be a string, but found {}",
                            clargs->serve.config.string(),
                            strategy->ToString());
                std::exit(kExitFailure);
            }
            auto s_value = ToTargetCacheWriteStrategy(strategy->String());
            if (not s_value) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nBuild key "
                    "\"target-cache write strategy\" has unknown value {}",
                    clargs->serve.config.string(),
                    strategy->ToString());
                std::exit(kExitFailure);
            }
            clargs->tc.target_cache_write_strategy = *s_value;
        }

        auto launcher = build_args->Get("local launcher", Expression::none_t{});
        if (launcher.IsNotNull()) {
            if (not launcher->IsList()) {
                Logger::Log(
                    LogLevel::Error,
                    "In serve service config file {}:\nValue for build key "
                    "\"local launcher\" has to be a list, but found {}",
                    clargs->serve.config.string(),
                    launcher->ToString());
                std::exit(kExitFailure);
            }
            std::vector<std::string> launcher_list{};
            for (auto const& entry : launcher->List()) {
                if (not entry->IsString()) {
                    Logger::Log(LogLevel::Error,
                                "In serve service config file {}:\nValue for "
                                "build key \"local launcher\" has to be a list "
                                "of string, but found {} with entry {}",
                                clargs->serve.config.string(),
                                launcher->ToString(),
                                entry->ToString());

                    std::exit(kExitFailure);
                }
                launcher_list.emplace_back(entry->String());
            }
            clargs->build.local_launcher = launcher_list;
        }
    }
}

#endif  // BOOTSTRAP_BUILD_TOOL
