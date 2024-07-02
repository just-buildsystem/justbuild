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

#ifndef INCLUDED_SRC_BUILDTOOL_AUTH_AUTHENTICATION_HPP
#define INCLUDED_SRC_BUILDTOOL_AUTH_AUTHENTICATION_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <streambuf>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

struct Auth final {
    struct TLS final {
        class Builder;

        // CA certificate bundle
        std::string const ca_cert = {};
        // Client-side signed certificate
        std::string const client_cert = {};
        // Client-side private key
        std::string const client_key = {};
        // Server-side signed certificate
        std::string const server_cert = {};
        // Server-side private key
        std::string const server_key = {};
    };

    std::variant<std::monostate, TLS> method = {};
};

class Auth::TLS::Builder final {
  public:
    auto SetCACertificate(
        std::optional<std::filesystem::path> cert_file) noexcept -> Builder& {
        ca_cert_file_ = std::move(cert_file);
        return *this;
    }

    auto SetClientCertificate(
        std::optional<std::filesystem::path> cert_file) noexcept -> Builder& {
        client_cert_file_ = std::move(cert_file);
        return *this;
    }

    auto SetClientKey(std::optional<std::filesystem::path> key_file) noexcept
        -> Builder& {
        client_key_file_ = std::move(key_file);
        return *this;
    }

    auto SetServerCertificate(
        std::optional<std::filesystem::path> cert_file) noexcept -> Builder& {
        server_cert_file_ = std::move(cert_file);
        return *this;
    }

    auto SetServerKey(std::optional<std::filesystem::path> key_file) noexcept
        -> Builder& {
        server_key_file_ = std::move(key_file);
        return *this;
    }

    /// \brief Finalize building, validate the entries, and create an Auth with
    /// TLS as method. Validation ensures that either both tls_client_cert or
    /// tls_client_key are set, or none of the two.
    /// \return Auth on success, error string on failure, nullopt if no TLS
    /// configuration fields were set.
    [[nodiscard]] auto Build() noexcept
        -> std::optional<expected<Auth, std::string>> {
        // To not duplicate default arguments of Auth::TLS in builder,
        // create a default config and copy default arguments from there.
        Auth::TLS const default_auth_tls;
        bool tls_args_exist = false;

        // Set and validate the CA certification.
        // If provided, the CA certificate bundle should of course not be empty.
        auto ca_cert = default_auth_tls.ca_cert;
        if (ca_cert_file_.has_value()) {
            if (auto content = read(*ca_cert_file_)) {
                if (content->empty()) {
                    return unexpected(
                        std::string("Please provide tls-ca-cert"));
                }
                ca_cert = *std::move(content);
                tls_args_exist = true;
            }
            else {
                return unexpected(
                    fmt::format("Could not read '{}' CA certificate.",
                                ca_cert_file_->string()));
            }
        }

        // Set and validate the client-side certification.
        // To enable mTLS, both tls_client_{certificate,key} must be supplied.
        auto client_cert = default_auth_tls.client_cert;
        if (client_cert_file_.has_value()) {
            if (auto content = read(*client_cert_file_)) {
                client_cert = *std::move(content);
                tls_args_exist = true;
            }
            else {
                return unexpected(
                    fmt::format("Could not read '{}' client certificate.",
                                client_cert_file_->string()));
            }
        }
        auto client_key = default_auth_tls.client_key;
        if (client_key_file_.has_value()) {
            if (auto content = read(*client_key_file_)) {
                client_key = *std::move(content);
                tls_args_exist = true;
            }
            else {
                return unexpected(fmt::format("Could not read '{}' client key.",
                                              client_key_file_->string()));
            }
        }
        if (client_cert.empty() != client_key.empty()) {
            std::string error = client_cert.empty()
                                    ? "Please also provide tls-client-cert"
                                    : "Please also provide tls-client-key";
            return unexpected(std::move(error));
        }

        // Set and validate the server-side certification.
        // To enable mTLS, both tls_server_{certificate,key} must be supplied.
        auto server_cert = default_auth_tls.server_cert;
        if (server_cert_file_.has_value()) {
            if (auto content = read(*server_cert_file_)) {
                server_cert = *std::move(content);
                tls_args_exist = true;
            }
            else {
                return unexpected(
                    fmt::format("Could not read '{}' server certificate.",
                                server_cert_file_->string()));
            }
        }
        auto server_key = default_auth_tls.server_key;
        if (server_key_file_.has_value()) {
            if (auto content = read(*server_key_file_)) {
                server_key = *std::move(content);
                tls_args_exist = true;
            }
            else {
                return unexpected(fmt::format("Could not read '{}' server key.",
                                              server_key_file_->string()));
            }
        }
        if (server_cert.empty() != server_key.empty()) {
            std::string error = server_cert.empty()
                                    ? "Please also provide tls-server-cert"
                                    : "Please also provide tls-server-key";
            return unexpected(std::move(error));
        }

        // If no TLS arguments were ever set, there is nothing to build.
        if (not tls_args_exist) {
            return std::nullopt;
        }

        // Return an authentication configuration with mTLS enabled.
        return Auth{.method = Auth::TLS{.ca_cert = std::move(ca_cert),
                                        .client_cert = std::move(client_cert),
                                        .client_key = std::move(client_key),
                                        .server_cert = std::move(server_cert),
                                        .server_key = std::move(server_key)}};
    }

  private:
    std::optional<std::filesystem::path> ca_cert_file_;
    std::optional<std::filesystem::path> client_cert_file_;
    std::optional<std::filesystem::path> client_key_file_;
    std::optional<std::filesystem::path> server_cert_file_;
    std::optional<std::filesystem::path> server_key_file_;

    /// \brief Auxiliary function to read the content of certification files.
    [[nodiscard]] static auto read(std::filesystem::path const& x) noexcept
        -> std::optional<std::string> {
        try {
            // if the file does not exist, it will throw an exception
            auto file = std::filesystem::canonical(x);
            std::ifstream cert{file};
            std::string tmp((std::istreambuf_iterator<char>(cert)),
                            std::istreambuf_iterator<char>());
            return tmp;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
        }
        return std::nullopt;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_AUTH_AUTHENTICATION_HPP
