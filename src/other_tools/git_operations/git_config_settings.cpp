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

/// \brief Tries to parse the given proxy string as an URL using the libcurl API
/// in a more permissive way, mirroring what git and curl internally do, then
/// returns the reconstructed URL if parsing succeeded, or a nullopt ProxyInfo.
/// Returns nullopt on unexpected errors.
[[nodiscard]] auto GetProxyAsPermissiveUrl(
    std::string const& proxy_url) noexcept -> std::optional<ProxyInfo> {
    // parse proxy string with permissive options:
    // use_non_support_scheme allows for non-standard schemes to be parsed;
    // use_guess_scheme tries to figure out the scheme from the hostname if none
    // is provided and defaults to http if it fails;
    auto parsed_url =
        CurlURLHandle::CreatePermissive(proxy_url,
                                        true /*use_guess_scheme*/,
                                        false /*use_default_scheme*/,
                                        true /*use_non_support_scheme*/);
    if (not parsed_url) {
        // exception encountered
        return std::nullopt;
    }
    if (not *parsed_url) {
        // failure to parse
        return ProxyInfo{std::nullopt};
    }
    // recombine the parsed url without changing it any further
    return ProxyInfo{parsed_url.value()->GetURL()};
}

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

auto GitConfigSettings::GetProxySettings(std::shared_ptr<git_config> const& cfg,
                                         std::string const& url,
                                         anon_logger_ptr const& logger)
    -> std::optional<ProxyInfo> {
    try {
        // perform proxy checks as git does
        git_buf tmp_buf{};  // temp buffer
        if (cfg != nullptr) {
            // parse given url
            auto parsed_url = CurlURLHandle::Create(url);
            if (not parsed_url) {
                // unexpected error occurred
                (*logger)(
                    "While getting proxy settings:\nfailed to parse remote URL",
                    true /*fatal*/);
                return std::nullopt;
            }
            if (parsed_url.value()) {
                // check for no_proxy envariable
                if (const char* envar = std::getenv("no_proxy")) {
                    // check if there is a pattern match
                    auto is_matched =
                        parsed_url.value()->NoproxyStringMatches(envar);
                    if (not is_matched) {
                        // unexpected error occurred
                        (*logger)(
                            "While getting proxy settings:\nmatching no_proxy "
                            "envariable patterns failed",
                            true /*fatal*/);
                        return std::nullopt;
                    }
                    if (is_matched == true) {
                        return ProxyInfo{std::nullopt};
                    }
                }
                // check for NO_PROXY envariable
                if (const char* envar = std::getenv("NO_PROXY")) {
                    // check if there is a pattern match
                    auto is_matched =
                        parsed_url.value()->NoproxyStringMatches(envar);
                    if (not is_matched) {
                        // unexpected error occurred
                        (*logger)(
                            "While getting proxy settings:\nmatching NO_PROXY "
                            "envariable patterns failed",
                            true /*fatal*/);
                        return std::nullopt;
                    }
                    if (is_matched == true) {
                        return ProxyInfo{std::nullopt};
                    }
                }
                // iterate over config entries of type
                // "http.<url>.proxy"
                git_config_iterator* iter_ptr{nullptr};
                if (git_config_iterator_glob_new(
                        &iter_ptr, cfg.get(), R"(http\..*\.proxy)") == 0) {
                    // wrap iterator
                    auto iter = std::unique_ptr<git_config_iterator,
                                                decltype(&config_iter_closer)>(
                        iter_ptr, config_iter_closer);
                    // set config key parsing offsets
                    const std::string::difference_type start_offset{
                        5};  // len("http.")
                    const std::string::difference_type end_offset{
                        6};  // len(".proxy")
                    // define ordered container storing matches of git
                    // config keys
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
                                "While getting proxy settings:\nmatching "
                                "config key failed",
                                true /*fatal*/);
                            return std::nullopt;
                        }
                        // store in ordered list only if a match occurred
                        if (match->matched) {
                            matches.emplace(*match, std::string(entry->value));
                        }
                    }
                    // look for any empty proxy value; if found, proxy is
                    // disabled
                    for (auto const& elem : matches) {
                        if (git_config_parse_path(&tmp_buf,
                                                  elem.second.c_str()) == 0) {
                            if (tmp_buf.size == 0) {
                                // cleanup memory
                                git_buf_dispose(&tmp_buf);
                                return ProxyInfo{std::nullopt};
                            }
                            // cleanup memory
                            git_buf_dispose(&tmp_buf);
                        }
                    }
                    // no_proxy checks are done, so look for actual proxy info;
                    // first, check the top "http.<url>.proxy" match
                    if (not matches.empty()) {
                        if (git_config_parse_path(
                                &tmp_buf, matches.begin()->second.c_str()) ==
                            0) {
                            auto tmp_str = std::string(tmp_buf.ptr);
                            // cleanup memory
                            git_buf_dispose(&tmp_buf);
                            // get proxy url in standard form
                            auto proxy_info = GetProxyAsPermissiveUrl(tmp_str);
                            if (not proxy_info) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting proxy "
                                    "settings:\npermissive parsing of "
                                    "remote-specific proxy URL failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            return proxy_info.value();
                        }
                    }
                    // check the generic "http.proxy" gitconfig entry;
                    // ignore errors
                    if (git_config_get_string_buf(
                            &tmp_buf, cfg.get(), R"(http.proxy)") == 0) {
                        if (tmp_buf.size > 0) {
                            auto tmp_str = std::string(tmp_buf.ptr);
                            // cleanup memory
                            git_buf_dispose(&tmp_buf);
                            // get proxy url in standard form
                            auto proxy_info = GetProxyAsPermissiveUrl(tmp_str);
                            if (not proxy_info) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting proxy settings:\npermissive "
                                    "parsing of http.proxy URL failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            return proxy_info.value();
                        }
                        // cleanup memory
                        git_buf_dispose(&tmp_buf);
                    }
                    // check proxy envariables depending on the scheme
                    auto url_scheme = parsed_url.value()->GetScheme();
                    if (not url_scheme) {
                        // unexpected behavior
                        (*logger)(
                            "While getting proxy settings:\nretrieving scheme "
                            "from parsed URL failed",
                            true /*fatal*/);
                        return std::nullopt;
                    }
                    if (url_scheme.value() == "https") {
                        // check https_proxy envariable
                        if (const char* envar = std::getenv("https_proxy")) {
                            // get proxy url in standard form
                            auto proxy_info = GetProxyAsPermissiveUrl(envar);
                            if (not proxy_info) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting proxy settings:\npermissive "
                                    "parsing of https_proxy envariable failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            return proxy_info.value();
                        }
                        // check HTTPS_PROXY envariable
                        if (const char* envar = std::getenv("HTTPS_PROXY")) {
                            // get proxy url in standard form
                            auto proxy_info = GetProxyAsPermissiveUrl(envar);
                            if (not proxy_info) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting proxy settings:\npermissive "
                                    "parsing of HTTPS_PROXY envariable failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            return proxy_info.value();
                        }
                    }
                    else if (url_scheme.value() == "http") {
                        // check http_proxy envariable
                        if (const char* envar = std::getenv("http_proxy")) {
                            // get proxy url in standard form
                            auto proxy_info = GetProxyAsPermissiveUrl(envar);
                            if (not proxy_info) {
                                // unexpected behavior
                                (*logger)(
                                    "While getting proxy settings:\npermissive "
                                    "parsing of http_proxy envariable failed",
                                    true /*fatal*/);
                                return std::nullopt;
                            }
                            return proxy_info.value();
                        }
                    }
                    // check all_proxy envariable
                    if (const char* envar = std::getenv("all_proxy")) {
                        // get proxy url in standard form
                        auto proxy_info = GetProxyAsPermissiveUrl(envar);
                        if (not proxy_info) {
                            // unexpected behavior
                            (*logger)(
                                "While getting proxy settings:\npermissive "
                                "parsing of all_proxy envariable failed",
                                true /*fatal*/);
                            return std::nullopt;
                        }
                        return proxy_info.value();
                    }
                    // check ALL_PROXY envariable
                    if (const char* envar = std::getenv("ALL_PROXY")) {
                        // get proxy url in standard form
                        auto proxy_info = GetProxyAsPermissiveUrl(envar);
                        if (not proxy_info) {
                            // unexpected behavior
                            (*logger)(
                                "While getting proxy settings:\npermissive "
                                "parsing of ALL_PROXY envariable failed",
                                true /*fatal*/);
                            return std::nullopt;
                        }
                        return proxy_info.value();
                    }
                }
            }
        }
        return ProxyInfo{std::nullopt};  // default to disabling proxy
    } catch (std::exception const& ex) {
        (*logger)(
            fmt::format("Getting proxy settings failed with:\n{}", ex.what()),
            true /*fatal*/);
        return std::nullopt;
    }
}
