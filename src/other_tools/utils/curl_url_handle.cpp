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

#include "src/other_tools/utils/curl_url_handle.hpp"

#include <regex>
#include <sstream>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

extern "C" {
#include "curl/curl.h"
}

void curl_url_closer(gsl::owner<CURLU*> handle) {
    curl_url_cleanup(handle);
}

namespace {

/// \brief Compares the two hosts as '.'-delimited substrings until there is a
/// mismatch. Wildcard ("*") matches any substring.
/// Returns a success flag.
[[nodiscard]] auto HostsMatch(std::string const& key_host,
                              std::string const& url_host) noexcept -> bool {
    // split key host
    std::vector<std::string> key_tokens{};
    std::string token{};
    std::istringstream iss(key_host);
    while (std::getline(iss, token, '.')) {
        key_tokens.emplace_back(token);
    }
    // split url host
    std::vector<std::string> url_tokens{};
    iss = std::istringstream{url_host};
    while (std::getline(iss, token, '.')) {
        url_tokens.emplace_back(token);
    }
    // number of tokens must match
    if (key_tokens.size() != url_tokens.size()) {
        return false;
    }
    // check for substring mismatch
    auto key_it = key_tokens.begin();
    auto url_it = url_tokens.begin();
    for (; key_it != key_tokens.end(); ++key_it, ++url_it) {
        if (*key_it != *url_it and *key_it != "*") {
            return false;
        }
    }
    return true;
}

/// \brief Compares the two paths as '/'-delimited substrings until there is a
/// mismatch or the end of the key path.
/// Returns the size of the key path if match successful, otherwise nullopt.
[[nodiscard]] auto PathMatchSize(std::string const& key_path,
                                 std::string const& url_path) noexcept
    -> std::optional<size_t> {
    // split key path
    std::vector<std::string> key_tokens{};
    std::string token{};
    std::istringstream iss(key_path);
    while (std::getline(iss, token, '/')) {
        key_tokens.emplace_back(token);
    }
    // split url path
    std::vector<std::string> url_tokens{};
    iss = std::istringstream{url_path};
    while (std::getline(iss, token, '/')) {
        url_tokens.emplace_back(token);
    }
    // key path should not have more tokens than the url path
    if (key_tokens.size() > url_tokens.size()) {
        return std::nullopt;
    }
    // check for substring mismatch
    auto key_it = key_tokens.begin();
    auto url_it = url_tokens.begin();
    for (; key_it != key_tokens.end(); ++key_it, ++url_it) {
        if (*key_it != *url_it) {
            return std::nullopt;
        }
    }
    // on success, return size of key path
    return key_path.size();
}

/// \brief Parses the given string according to the scheme:
///    [[.]<dot-separated-host-prefixes>.]<domain>[:<port>]
/// The parsing ignores a single leading '.' character, if present.
/// Does not perform any other validity check (e.g., for port value).
[[nodiscard]] auto ParseNoproxyPattern(std::string const& pattern) noexcept
    -> NoproxyPattern {
    // get the host part
    std::string host{};
    std::istringstream iss(pattern);
    std::getline(iss, host, ':');  // stop at port part or end of string
    // check if port part exists
    std::optional<std::string> port{std::nullopt};
    if (host.size() != pattern.size()) {
        port = std::string(
            pattern.begin() +
                static_cast<std::string::difference_type>(host.size()) + 1,
            pattern.end());
    }
    // remove one leading '.' char from host part, if present
    if (host[0] == '.') {
        host = std::string(host.begin() + 1, host.end());
    }
    // split the host part
    std::vector<std::string> host_tokens{};
    std::string token{};
    iss = std::istringstream(host);
    while (std::getline(iss, token, '.')) {
        host_tokens.emplace_back(token);
    }
    return NoproxyPattern{host_tokens, port};
}

/// \brief Check whether a given test pattern matches a target pattern with
/// respect to the matching rules for the no_proxy envariable.
[[nodiscard]] auto NoproxyPatternMatches(NoproxyPattern const& test_pattern,
                                         NoproxyPattern const& target_pattern)
    -> bool {
    // check if port matches, if given
    if (test_pattern.port and test_pattern.port != target_pattern.port) {
        return false;
    }
    // host tokens must exist
    if (test_pattern.host_tokens.empty() or
        target_pattern.host_tokens.empty() or
        test_pattern.host_tokens.size() > target_pattern.host_tokens.size()) {
        return false;
    }
    // check if the host/domain substrings match, in reverse order
    auto test_it = test_pattern.host_tokens.end() - 1;
    auto target_it = target_pattern.host_tokens.end() - 1;
    for (; test_it != test_pattern.host_tokens.begin() - 1;
         --test_it, --target_it) {
        if (*test_it != *target_it) {
            return false;
        }
    }
    return true;
}

}  // namespace

auto CurlURLHandle::Create(std::string const& url) noexcept
    -> std::optional<CurlURLHandlePtr> {
    try {
        auto url_h = std::make_shared<CurlURLHandle>();
        auto* handle = curl_url();
        // try to parse the given url
        auto rc = curl_url_set(handle, CURLUPART_URL, url.c_str(), 0U);
        if (rc != CURLUE_OK) {
            Logger::Log(LogLevel::Debug,
                        "CurlURLHandle: parsing URL {} failed with:\n{}",
                        url,
                        curl_url_strerror(rc));
            curl_url_cleanup(handle);
            return nullptr;
        }
        url_h->handle_.reset(handle);
        return std::make_optional<CurlURLHandlePtr>(url_h);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "CurlURLHandle: creating curl URL handle failed "
                    "unexpectedly with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::CreatePermissive(std::string const& url,
                                     bool use_guess_scheme,
                                     bool use_default_scheme,
                                     bool use_non_support_scheme,
                                     bool use_no_authority,
                                     bool use_path_as_is,
                                     bool use_allow_space,
                                     bool ignore_fatal) noexcept
    -> std::optional<CurlURLHandlePtr> {
    try {
        auto url_h = std::make_shared<CurlURLHandle>();
        auto* handle = curl_url();
        // set up flags
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        auto flags{use_guess_scheme ? CURLU_GUESS_SCHEME : 0U};
        if (use_default_scheme) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_DEFAULT_SCHEME;
        }
        if (use_non_support_scheme) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_NON_SUPPORT_SCHEME;
        }
        if (use_no_authority) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_NO_AUTHORITY;
        }
        if (use_path_as_is) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_PATH_AS_IS;
        }
        if (use_allow_space) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_ALLOW_SPACE;
        }
        // try to parse the given url
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        auto rc = curl_url_set(handle, CURLUPART_URL, url.c_str(), flags);
        if (rc != CURLUE_OK) {
            Logger::Log(
                LogLevel::Debug,
                "CurlURLHandle: parsing URL permissively failed with:\n{}",
                curl_url_strerror(rc));
            curl_url_cleanup(handle);
            return nullptr;
        }
        url_h->handle_.reset(handle);
        return std::make_optional<CurlURLHandlePtr>(url_h);
    } catch (std::exception const& ex) {
        Logger::Log(ignore_fatal ? LogLevel::Debug : LogLevel::Error,
                    "CurlURLHandle: creating permissive curl URL handle failed "
                    "unexpectedly with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::Duplicate() noexcept -> CurlURLHandlePtr {
    try {
        auto url_h = std::make_shared<CurlURLHandle>();
        url_h->handle_.reset(curl_url_dup(handle_.get()));
        return url_h;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "CurlURLHandle: duplicating curl URL handle failed "
                    "unexpectedly with:\n{}",
                    ex.what());
        return nullptr;
    }
}

auto CurlURLHandle::GetURL(bool use_default_port,
                           bool use_default_scheme,
                           bool use_no_default_port,
                           bool ignore_fatal) noexcept
    -> std::optional<std::string> {
    try {
        // set up flags
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        auto flags{use_default_port ? CURLU_DEFAULT_PORT : 0U};
        if (use_default_scheme) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_DEFAULT_SCHEME;
        }
        if (use_no_default_port) {
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            flags |= CURLU_NO_DEFAULT_PORT;
        }
        // get the URL
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url = nullptr;
        auto rc = curl_url_get(handle_.get(), CURLUPART_URL, &url, flags);
        if (rc != CURLUE_OK) {
            Logger::Log(ignore_fatal ? LogLevel::Debug : LogLevel::Error,
                        "CurlURLHandle: retrieving URL failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        std::string url_str{url};
        // free memory
        curl_free(url);
        return url_str;
    } catch (std::exception const& ex) {
        Logger::Log(
            ignore_fatal ? LogLevel::Debug : LogLevel::Error,
            "CurlURLHandle: retrieving URL failed unexpectedly with:\n{}",
            ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::GetScheme(bool use_default_scheme) noexcept
    -> std::optional<OptionalString> {
    try {
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        auto flags{use_default_scheme ? CURLU_DEFAULT_SCHEME : 0U};
        // get the scheme
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* scheme = nullptr;
        auto rc = curl_url_get(handle_.get(), CURLUPART_SCHEME, &scheme, flags);
        if (rc != CURLUE_OK and rc != CURLUE_NO_SCHEME) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving scheme failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        auto res = OptionalString{std::nullopt};
        if (rc != CURLUE_NO_SCHEME) {
            res = OptionalString{std::string{scheme}};
        }
        // free memory
        curl_free(scheme);
        return res;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "CurlURLHandle: retrieving scheme failed unexpectedly with:\n{}",
            ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::GetConfigStructFromKey(std::string const& key) noexcept
    -> std::optional<GitConfigKeyPtr> {
    try {
        auto parsed_key = Create(key);
        if (not parsed_key) {
            return std::nullopt;  // report exception
        }
        if (*parsed_key == nullptr) {
            return nullptr;  // unparsable key
        }
        // populate all useful components
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* field = nullptr;
        auto* h = parsed_key.value()->handle_.get();
        auto gconfig = std::make_shared<GitConfigKey>();

        auto rc = curl_url_get(h, CURLUPART_SCHEME, &field, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_SCHEME) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving scheme in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_SCHEME) {
            gconfig->scheme = std::string(field);
        }
        curl_free(field);
        field = nullptr;

        rc = curl_url_get(h, CURLUPART_USER, &field, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_USER) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving user in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_USER) {
            gconfig->user = std::string(field);
        }
        curl_free(field);
        field = nullptr;

        rc = curl_url_get(h, CURLUPART_HOST, &field, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_HOST) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving host in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_HOST) {
            gconfig->host = std::string(field);
        }
        curl_free(field);
        field = nullptr;

        rc = curl_url_get(h,
                          CURLUPART_PORT,
                          &field,
                          // NOLINTNEXTLINE(hicpp-signed-bitwise)
                          CURLU_DEFAULT_PORT);  // enforce port existence
        if (rc != CURLUE_OK and rc != CURLUE_NO_PORT) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving port in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_PORT) {
            gconfig->port = std::string(field);
        }
        curl_free(field);
        field = nullptr;

        // stored path will contain also query and fragment, if existing, and
        // must end with a '/'
        rc = curl_url_get(h, CURLUPART_PATH, &field, 0U);
        if (rc != CURLUE_OK) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving path in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        auto running_path = std::filesystem::path{"/"} / std::string(field);
        curl_free(field);
        field = nullptr;

        rc = curl_url_get(h, CURLUPART_QUERY, &field, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_QUERY) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving query in get config struct "
                        "failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_QUERY) {
            running_path += std::string("?") + std::string(field);
        }
        curl_free(field);
        field = nullptr;

        rc = curl_url_get(h, CURLUPART_FRAGMENT, &field, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_FRAGMENT) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving fragment in get config "
                        "struct failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        if (rc != CURLUE_NO_FRAGMENT) {
            running_path += std::string("#") + std::string(field);
        }
        curl_free(field);

        running_path /= "";  // make sure it ends with a '/'
        gconfig->path = running_path;

        return gconfig;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "CurlURLHandle: get config struct from parsed key failed "
                    "unexpectedly with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::ParseConfigKey(std::string const& key) noexcept
    -> std::optional<GitConfigKeyPtr> {
    try {
        // if key has no asterisks, parse as usual
        if (key.find('*') == std::string::npos) {
            return GetConfigStructFromKey(key);
        }

        // replace all '*' wildcards with '.'
        std::string tmp_key{key};
        std::replace(tmp_key.begin(), tmp_key.end(), '*', '.');

        // parse and extract hostname
        auto tmp_parsed = Create(tmp_key);
        if (not tmp_parsed) {
            return std::nullopt;  // exception
        }
        if (tmp_parsed == nullptr) {
            return nullptr;  // unparsable
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* host_ptr = nullptr;
        auto rc = curl_url_get(
            tmp_parsed.value()->handle_.get(), CURLUPART_HOST, &host_ptr, 0U);
        if (rc != CURLUE_OK) {
            Logger::Log(
                LogLevel::Error,
                "CurlURLHandle: retrieving host in parse config key failed "
                "with:\n{}",
                curl_url_strerror(rc));
            return std::nullopt;
        }
        std::string parsed_host{host_ptr};
        curl_free(host_ptr);  // release memory

        // create regex to find all possible matches of the parsed host in the
        // original key, where any '.' can also be a '*'
        std::stringstream pattern{};
        size_t old_index{};
        size_t index{};
        while ((index = parsed_host.find('.', old_index)) !=
               std::string::npos) {
            pattern << parsed_host.substr(old_index, index - old_index);
            pattern << R"([\.\*])";
            old_index = index + 1;
        }
        pattern << parsed_host.substr(old_index);
        std::regex re(pattern.str());

        // for every match, replace the parsed host in the found position and
        // try to parse as usual
        size_t host_len = parsed_host.length();
        for (auto it = std::sregex_iterator(key.begin(), key.end(), re);
             it != std::sregex_iterator();
             ++it) {
            std::string new_key{key};
            new_key.replace(
                static_cast<size_t>(it->position()), host_len, parsed_host);

            // try to parse new key
            auto try_config_key = GetConfigStructFromKey(new_key);
            if (try_config_key and *try_config_key != nullptr) {
                // replace the parsed hostname with the match
                try_config_key.value()->host = it->str();
                return try_config_key;
            }
        }
        // no match was parsable
        return nullptr;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "CurlURLHandle: parse config key failed unexpectedly with:\n{}",
            ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::MatchConfigKey(std::string const& key) noexcept
    -> std::optional<ConfigKeyMatchDegree> {
    try {
        size_t host_len{};
        bool user_matched{false};

        // parse the given key
        auto parsed_key = ParseConfigKey(key);
        if (not parsed_key) {
            return std::nullopt;  // an exception occurred that shouldn't have
        }
        if (*parsed_key == nullptr) {
            return ConfigKeyMatchDegree{};  // non-parsable, so return no match
        }

        // check that scheme matches
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_scheme = nullptr;
        auto rc =
            curl_url_get(handle_.get(), CURLUPART_SCHEME, &url_scheme, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_SCHEME) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url scheme in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        auto url_scheme_str = url_scheme == nullptr
                                  ? std::nullopt
                                  : std::make_optional<std::string>(url_scheme);
        curl_free(url_scheme);

        if (parsed_key.value()->scheme != url_scheme_str) {
            return ConfigKeyMatchDegree{};  // mismatch
        }

        // check the user, if the config key has the field
        if (parsed_key.value()->user) {
            // check the user field in stored URL
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
            char* url_user = nullptr;
            rc = curl_url_get(handle_.get(), CURLUPART_USER, &url_user, 0U);
            if (rc != CURLUE_OK) {  // if key has user field, url must as well
                Logger::Log(LogLevel::Error,
                            "CurlURLHandle: retrieving url user in matching "
                            "config key failed with:\n{}",
                            curl_url_strerror(rc));
                return std::nullopt;
            }
            auto url_user_str = url_user == nullptr
                                    ? std::nullopt
                                    : std::make_optional<std::string>(url_user);
            curl_free(url_user);

            if (not url_user_str or parsed_key.value()->user != *url_user_str) {
                return ConfigKeyMatchDegree{};  // mismatch
            }
            // signal the match
            user_matched = true;
        }

        // check that host/domain name matches
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_host = nullptr;
        rc = curl_url_get(handle_.get(), CURLUPART_HOST, &url_host, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_HOST) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url host in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        auto url_host_str = url_host == nullptr
                                ? std::nullopt
                                : std::make_optional<std::string>(url_host);
        curl_free(url_host);

        if (parsed_key.value()->host != url_host_str) {
            if (not(parsed_key.value()->host and url_host_str and
                    HostsMatch(parsed_key.value()->host.value(),
                               *url_host_str))) {
                return ConfigKeyMatchDegree{};  // mismatch
            }
        }
        // store matched host length
        host_len +=
            parsed_key.value()->host ? parsed_key.value()->host->size() : 0U;

        // check port match; get with default value if not existing
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_port = nullptr;
        rc = curl_url_get(handle_.get(),
                          CURLUPART_PORT,
                          &url_port,
                          // NOLINTNEXTLINE(hicpp-signed-bitwise)
                          CURLU_DEFAULT_PORT);  // enforce port existence
        if (rc != CURLUE_OK) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url port in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        auto url_port_str = url_port == nullptr
                                ? std::nullopt
                                : std::make_optional<std::string>(url_port);
        curl_free(url_port);

        if (parsed_key.value()->port != url_port_str) {
            return ConfigKeyMatchDegree{};  // mismatch
        }

        // check path match; this is done up to any '/'-delimited prefix;
        // we need the complete path, so path + query + fragment (if existing)
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_path = nullptr;
        rc = curl_url_get(handle_.get(), CURLUPART_PATH, &url_path, 0U);
        if (rc != CURLUE_OK) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url path in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        // parsed path is never empty
        auto url_path_str = std::filesystem::path{"/"} / std::string(url_path);
        curl_free(url_path);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_query = nullptr;
        rc = curl_url_get(handle_.get(), CURLUPART_QUERY, &url_query, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_QUERY) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url query in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        // append to path
        url_path_str += url_query == nullptr
                            ? std::string()
                            : std::string("?") + std::string(url_query);
        curl_free(url_query);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_fragment = nullptr;
        rc = curl_url_get(handle_.get(), CURLUPART_FRAGMENT, &url_fragment, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_FRAGMENT) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url fragment in matching "
                        "config key failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        // append to path
        url_path_str += url_fragment == nullptr
                            ? std::string()
                            : std::string("#") + std::string(url_fragment);
        curl_free(url_fragment);

        // make sure path ends with '/' for comparison purposes
        url_path_str /= "";

        auto path_len = PathMatchSize(parsed_key.value()->path.string(),
                                      url_path_str.string());
        if (not path_len) {
            return ConfigKeyMatchDegree{};  // paths do not match
        }

        // key matches; success!
        return ConfigKeyMatchDegree{
            true /*matched*/, host_len, *path_len, user_matched};
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error,
            "CurlURLHandle: match config key failed unexpectedly with:\n{}",
            ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::NoproxyStringMatches(std::string const& no_proxy) noexcept
    -> std::optional<bool> {
    try {
        // split no_proxy string by both spaces and commas
        std::vector<std::string> patterns{};
        std::string token1{};
        std::istringstream iss1(no_proxy);
        // split by spaces
        while (std::getline(iss1, token1, ' ')) {
            std::istringstream iss2(token1);
            std::string token2{};
            // for each such token, split by commas
            while (std::getline(iss2, token2, ',')) {
                if (not token2.empty()) {
                    patterns.emplace_back(token2);
                }
            }
        }
        // get the stored URL host (mandatory) and port (optional) as a
        // NoproxyPattern object
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_host = nullptr;
        auto rc = curl_url_get(handle_.get(), CURLUPART_HOST, &url_host, 0U);
        if (rc != CURLUE_OK) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url host in no_proxy string "
                        "matching failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        std::string tmp_pattern{url_host};
        curl_free(url_host);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* url_port = nullptr;
        rc = curl_url_get(handle_.get(), CURLUPART_PORT, &url_port, 0U);
        if (rc != CURLUE_OK and rc != CURLUE_NO_PORT) {
            Logger::Log(LogLevel::Error,
                        "CurlURLHandle: retrieving url port in no_proxy string "
                        "matching failed with:\n{}",
                        curl_url_strerror(rc));
            return std::nullopt;
        }
        // it's simpler to (re)use the existing pattern parser
        if (url_port != nullptr) {
            tmp_pattern += ":";
            tmp_pattern += std::string(url_port);
            curl_free(url_port);
        }
        auto url_hostport_as_pattern = ParseNoproxyPattern(tmp_pattern);

        // check for match with any pattern
        for (auto const& pattern : patterns) {
            // ignore an empty pattern
            if (pattern.empty()) {
                continue;
            }
            // check for trivial wildcard
            if (pattern == "*") {
                return true;
            }
            // parse pattern and check for match
            auto parsed_pattern = ParseNoproxyPattern(pattern);
            if (NoproxyPatternMatches(parsed_pattern,
                                      url_hostport_as_pattern)) {
                return true;
            }
        }
        return false;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "CurlURLHandle: no_proxy string matching failed "
                    "unexpectedly with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto CurlURLHandle::GetHostname(std::string const& url) noexcept
    -> std::optional<std::string> {
    try {
        // Allow parsing spaces in path (we only care about hostname), and do
        // not log error on failure.
        if (auto parsed_url = CreatePermissive(url,
                                               false /*use_guess_scheme*/,
                                               false /*use_default_scheme*/,
                                               false /*use_non_support_scheme*/,
                                               false /*use_no_authority*/,
                                               false /*use_path_as_is*/,
                                               true /*use_allow_space*/,
                                               true /*ignore_fatal*/)) {
            if (*parsed_url == nullptr) {
                return std::nullopt;
            }

            char* buffer{nullptr};  // NOLINT
            auto rc = curl_url_get(
                parsed_url.value()->handle_.get(), CURLUPART_HOST, &buffer, 0U);

            std::string hostname{};
            if (buffer != nullptr) {
                hostname = std::string{buffer};
                curl_free(buffer);
            }

            if (rc != CURLUE_OK) {
                Logger::Log(LogLevel::Debug,
                            "CurlURLHandle: getting hostname from URL {} "
                            "failed with:\n{}",
                            url,
                            curl_url_strerror(rc));
                return std::nullopt;
            }
            return hostname;
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Debug,
            "CurlURLHandle: Getting hostname from URL failed unexpectedly "
            "with:\n{}",
            ex.what());
    }
    return std::nullopt;
}
