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
#include "src/buildtool/logging/logger.hpp"
enum class AuthMethod : std::uint8_t { kNONE, kTLS };

class Auth {
  public:
    [[nodiscard]] static auto Instance() noexcept -> Auth& {
        static Auth instance{};
        return instance;
    }

    static void SetAuthMethod(AuthMethod x) { Instance().auth_ = x; }
    [[nodiscard]] static auto GetAuthMethod() noexcept -> AuthMethod {
        return Instance().auth_;
    }

    class TLS {
      public:
        [[nodiscard]] static auto Instance() noexcept -> TLS& {
            static TLS instance{};
            return instance;
        }

        [[nodiscard]] static auto CACert() noexcept -> const std::string& {
            return Instance().ca_cert_;
        }

        [[nodiscard]] static auto ClientCert() noexcept -> const std::string& {
            return Instance().client_cert_;
        }

        [[nodiscard]] static auto ClientKey() noexcept -> const std::string& {
            return Instance().client_key_;
        }

        [[nodiscard]] static auto ServerCert() noexcept -> const std::string& {
            return Instance().server_cert_;
        }

        [[nodiscard]] static auto ServerKey() noexcept -> const std::string& {
            return Instance().server_key_;
        }

        [[nodiscard]] static auto SetCACertificate(
            std::filesystem::path const& cert_file) noexcept -> bool {
            return set(cert_file, &Instance().ca_cert_);
        }

        [[nodiscard]] static auto SetClientCertificate(
            std::filesystem::path const& cert_file) noexcept -> bool {
            return set(cert_file, &Instance().client_cert_);
        }

        [[nodiscard]] static auto SetClientKey(
            std::filesystem::path const& key_file) noexcept -> bool {
            return set(key_file, &Instance().client_key_);
        }

        [[nodiscard]] static auto SetServerCertificate(
            std::filesystem::path const& cert_file) noexcept -> bool {
            return set(cert_file, &Instance().server_cert_);
        }

        [[nodiscard]] static auto SetServerKey(
            std::filesystem::path const& key_file) noexcept -> bool {
            return set(key_file, &Instance().server_key_);
        }
        // must be called after the parsing of cmd line arguments
        // we ensure that either both tls_client_cert or tls_client_key are set
        // or none of the two.
        [[nodiscard]] static auto Validate() noexcept -> bool {
            if (CACert().empty()) {
                Logger::Log(LogLevel::Error, "Please provide tls-ca-cert");
                return false;
            }

            // to enable mTLS, both tls_client_{certificate,key} must be
            // supplied
            if (ClientCert().empty() && not(ClientKey().empty())) {
                Logger::Log(LogLevel::Error,
                            "Please also provide tls-client-cert");
                return false;
            }
            if (not(ClientCert().empty()) && ClientKey().empty()) {
                Logger::Log(LogLevel::Error,
                            "Please also provide tls-client-key");
                return false;
            }

            // to enable mTLS, both tls_server_{certificate,key} must be
            // supplied
            if (ServerCert().empty() && not(ServerKey().empty())) {
                Logger::Log(LogLevel::Error,
                            "Please also provide tls-server-cert");
                return false;
            }
            if (not(ServerCert().empty()) && ServerKey().empty()) {
                Logger::Log(LogLevel::Error,
                            "Please also provide tls-server-key");
                return false;
            }
            return true;
        }

      private:
        std::string ca_cert_;
        std::string client_cert_;
        std::string client_key_;
        std::string server_cert_;
        std::string server_key_;
        // auxiliary function to set the content of the members of this class
        [[nodiscard]] static auto set(
            std::filesystem::path const& x,
            gsl::not_null<std::string*> const& member) noexcept -> bool {
            Auth::SetAuthMethod(AuthMethod::kTLS);
            try {
                // if the file does not exist, it will throw an exception
                auto file = std::filesystem::canonical(x);
                std::ifstream cert{file};
                std::string tmp((std::istreambuf_iterator<char>(cert)),
                                std::istreambuf_iterator<char>());
                *member = std::move(tmp);
            } catch (std::exception const& e) {
                Logger::Log(LogLevel::Error, e.what());
                return false;
            }
            return true;
        }
    };

  private:
    AuthMethod auth_{AuthMethod::kNONE};
};
#endif
