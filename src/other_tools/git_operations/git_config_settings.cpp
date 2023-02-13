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

#include "src/other_tools/git_operations/git_config_settings.hpp"

#include <map>

#include "fmt/core.h"
#include "src/other_tools/utils/curl_url_handle.hpp"

extern "C" {
#include <git2.h>
}

namespace {

void config_iter_closer(gsl::owner<git_config_iterator*> iter) {
    git_config_iterator_free(iter);
}

// callback to enable SSL certificate check for remote fetch
const auto certificate_check_cb = [](git_cert* /*cert*/,
                                     int /*valid*/,
                                     const char* /*host*/,
                                     void* /*payload*/) -> int { return 1; };

// callback to remote fetch without an SSL certificate check
const auto certificate_passthrough_cb = [](git_cert* /*cert*/,
                                           int /*valid*/,
                                           const char* /*host*/,
                                           void* /*payload*/) -> int {
    return 0;
};

/// \brief Custom comparison of matching degrees. Return true if left argument's
/// degree of matching is better that the right argument's. When both are
/// equally good matches, return true to make the latest entry win.
struct ConfigKeyMatchCompare {
    [[nodiscard]] auto operator()(ConfigKeyMatchDegree const& left,
                                  ConfigKeyMatchDegree const& right) const
        -> bool {
        if (left.host_len != right.host_len) {
            return left.host_len > right.host_len;
        }
        if (left.path_len != right.path_len) {
            return left.path_len > right.path_len;
        }
        if (left.user_matched != right.user_matched) {
            return left.user_matched;
        }
        return true;
    }
};

}  // namespace

auto GitConfigSettings::GetSSLCallback(std::shared_ptr<git_config> const& cfg,
                                       std::string const& url,
                                       anon_logger_ptr const& logger)
    -> std::optional<git_transport_certificate_check_cb> {
    try {
        // check SSL verification settings, from most to least specific
        std::optional<bool> check_cert{std::nullopt};
        int tmp{};
        // check if GIT_SSL_NO_VERIFY envariable is set (value is
        // irrelevant)
        const char* ssl_no_verify_var{std::getenv("GIT_SSL_NO_VERIFY")};
        if (ssl_no_verify_var != nullptr) {
            check_cert = false;
        }
        else {
            if (cfg != nullptr) {
                // check all the url-specific gitconfig entries; if any key
                // url matches, use the respective gitconfig entry value
                auto parsed_url = CurlURLHandle::Create(url);
                if (not parsed_url) {
                    // unexpected error occurred
                    (*logger)(
                        "While getting SSL callback:\nfailed to parse remote "
                        "URL",
                        true /*fatal*/);
                    return std::nullopt;
                }
                if (*parsed_url) {
                    // iterate over config entries of type
                    // "http.<url>.sslVerify"
                    git_config_iterator* iter_ptr{nullptr};
                    if (git_config_iterator_glob_new(
                            &iter_ptr, cfg.get(), R"(http\..*\.sslverify)") ==
                        0) {
                        // wrap iterator
                        auto iter =
                            std::unique_ptr<git_config_iterator,
                                            decltype(&config_iter_closer)>(
                                iter_ptr, config_iter_closer);
                        // set config key parsing offsets
                        const std::string::difference_type start_offset{
                            5};  // len("http.")
                        const std::string::difference_type end_offset{
                            10};  // len(".sslverify")
                        // define ordered container storing matches
                        std::map<ConfigKeyMatchDegree,
                                 std::string,
                                 ConfigKeyMatchCompare>
                            matches{};
                        // iterate through config keys
                        git_config_entry* entry{nullptr};
                        while (git_config_next(&entry, iter.get()) == 0) {
                            // get the url part of the config key
                            std::string entry_name{entry->name};
                            auto entry_url =
                                std::string(entry_name.begin() + start_offset,
                                            entry_name.end() - end_offset);
                            // get match degree
                            auto match =
                                parsed_url.value()->MatchConfigKey(entry_url);
                            if (not match) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting SSL callback:\nmatching "
                                    "config key failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            // store in ordered list only if a match
                            // occurred
                            if (match->matched) {
                                matches.emplace(*match,
                                                std::string(entry->value));
                            }
                        }
                        // if at least one match occurred, use the best one
                        if (not matches.empty()) {
                            if (git_config_parse_bool(
                                    &tmp, matches.begin()->second.c_str()) ==
                                0) {
                                check_cert = tmp == 1;
                            }
                        }
                    }
                }
                if (not check_cert) {
                    // check the generic gitconfig entry; ignore errors
                    if (git_config_get_bool(
                            &tmp, cfg.get(), R"(http.sslverify)") == 0) {
                        check_cert = tmp == 1;
                    }
                }
            }
        }
        // set callback: passthrough only if check_cert is false
        return (check_cert and not *check_cert) ? certificate_passthrough_cb
                                                : certificate_check_cb;
    } catch (std::exception const& ex) {
        (*logger)(
            fmt::format("Getting SSL callback failed with:\n{}", ex.what()),
            true /*fatal*/);
        return std::nullopt;
    }
}
