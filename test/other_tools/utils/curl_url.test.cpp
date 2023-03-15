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

#include "catch2/catch_test_macros.hpp"
#include "src/other_tools/utils/curl_url_handle.hpp"

TEST_CASE("Curl URL handle basics", "[curl_url_handle_basics]") {
    SECTION("Parse URL") {
        // full syntax check
        auto url_h_full = CurlURLHandle::Create(
            "https://user:pass@example.com:50000/some/"
            "pa.th?what=what&who=who#fragment");
        CHECK(url_h_full);
        CHECK(*url_h_full);
        // bare bone syntax check
        auto url_h_thin = CurlURLHandle::Create("http://example.com");
        CHECK(url_h_thin);
        CHECK(*url_h_thin);
        // double dots in hostname
        auto url_h_double_dots = CurlURLHandle::Create("http://..example..com");
        CHECK(url_h_double_dots);
        CHECK(*url_h_double_dots);
        // fail check
        auto url_h_fail = CurlURLHandle::Create("file://foo:50505");
        CHECK(url_h_fail);
        CHECK(not *url_h_fail);
    }

    SECTION("Get URL") {
        auto url_h = CurlURLHandle::Create("http://example.com:80");
        REQUIRE(url_h);
        REQUIRE(*url_h);

        // get with default opts
        auto ret_url = url_h.value()->GetURL();
        REQUIRE(ret_url);
        CHECK(*ret_url == "http://example.com:80/");

        // get with no default port
        ret_url = url_h.value()->GetURL(false, /* use_default_port */
                                        false, /* use_default_scheme */
                                        true   /* use_no_default_port */
        );
        REQUIRE(ret_url);
        CHECK(*ret_url == "http://example.com/");
    }

    SECTION("Get scheme of URL") {
        auto url_h = CurlURLHandle::Create("http://example.com:80");
        REQUIRE(url_h);
        REQUIRE(*url_h);

        auto ret_url = url_h.value()->GetScheme();
        REQUIRE(ret_url);
        CHECK(*ret_url == "http");
    }

    SECTION("Duplicate URL") {
        auto url_h = CurlURLHandle::Create("http://example.com");
        REQUIRE(url_h);
        REQUIRE(*url_h);
        auto url_h_dup = url_h.value()->Duplicate();
        REQUIRE(url_h_dup);
        CHECK(url_h.value()->GetURL() == url_h_dup->GetURL());
    }

    SECTION("Parse URL with permissive arguments") {
        // guess scheme from hostname
        auto url_h_guess_scheme =
            CurlURLHandle::CreatePermissive("ftp.example.com", true);
        CHECK(url_h_guess_scheme);
        CHECK(*url_h_guess_scheme);
        // check what was stored: path as-is stops after first slash
        auto ret_url_guess_scheme = url_h_guess_scheme.value()->GetURL();
        REQUIRE(ret_url_guess_scheme);
        CHECK(*ret_url_guess_scheme == "ftp://ftp.example.com/");

        // non-supported scheme, no authority, path not normalized
        auto url_h_nonstandard_scheme = CurlURLHandle::CreatePermissive(
            "socks5:///foo/../bar#boo", false, false, true, true, true);
        CHECK(url_h_nonstandard_scheme);
        CHECK(*url_h_nonstandard_scheme);
        // check what was stored: path as-is but without preceding slash
        auto ret_url_nonstandard_scheme =
            url_h_nonstandard_scheme.value()->GetURL();
        REQUIRE(ret_url_nonstandard_scheme);
        CHECK(*ret_url_nonstandard_scheme == "socks5://foo/../bar#boo");

        // bare IP treated with http scheme as default works (proxy-style)
        auto url_h_bare_ip =
            CurlURLHandle::CreatePermissive("192.0.2.1", true, false, true);
        CHECK(url_h_bare_ip);
        CHECK(*url_h_bare_ip);
        // check what was stored: defaults to http
        auto ret_url_bare_ip = url_h_bare_ip.value()->GetURL();
        REQUIRE(ret_url_bare_ip);
        CHECK(*ret_url_bare_ip == "http://192.0.2.1/");

        // default scheme, no authority, path not normalized
        auto url_h_empty =
            CurlURLHandle::CreatePermissive("", false, true, false, true, true);
        CHECK(url_h_empty);
        CHECK(*url_h_empty);
        // check what was stored: scheme http, path single slash
        auto ret_url_empty = url_h_empty.value()->GetURL();
        REQUIRE(ret_url_empty);
        CHECK(*ret_url_empty == "https:///");
    }

    SECTION("Parse config key") {
        auto key_h = CurlURLHandle::ParseConfigKey(
            "http://user@*.com/foo/bar?query#fragment");
        CHECK(key_h);
        REQUIRE(*key_h);

        // check exact fields
        CHECK(key_h.value()->scheme);
        CHECK(key_h.value()->scheme.value() == "http");
        CHECK(key_h.value()->user);
        CHECK(key_h.value()->user.value() == "user");
        CHECK(key_h.value()->host);
        CHECK(key_h.value()->host.value() == "*.com");
        CHECK(key_h.value()->port);
        CHECK(key_h.value()->port.value() == "80");  // default http port
        CHECK(key_h.value()->path.string() == "/foo/bar?query#fragment/");
    }
}

TEST_CASE("Curl URL match config key", "[curl_url_match_config_key]") {
    auto url_h =
        CurlURLHandle::Create("http://user@example.com/foo/bar?query#fragment");
    REQUIRE(url_h);
    REQUIRE(*url_h);

    SECTION("Match exactly") {
        auto match_exactly = url_h.value()->MatchConfigKey(
            "http://user@example.com/foo/bar?query#fragment");
        REQUIRE(match_exactly);

        CHECK(match_exactly->matched);
        CHECK(match_exactly->host_len == 11);
        CHECK(match_exactly->path_len == 24);
        CHECK(match_exactly->user_matched);
    }

    SECTION("Match without user") {
        auto match_wo_user = url_h.value()->MatchConfigKey(
            "http://example.com/foo/bar?query#fragment");
        REQUIRE(match_wo_user);

        CHECK(match_wo_user->matched);
        CHECK(match_wo_user->host_len == 11);
        CHECK(match_wo_user->path_len == 24);
        CHECK(not match_wo_user->user_matched);
    }

    SECTION("Match with default port") {
        auto match_default_port = url_h.value()->MatchConfigKey(
            "http://user@example.com:80/foo/bar?query#fragment");
        REQUIRE(match_default_port);

        CHECK(match_default_port->matched);
        CHECK(match_default_port->host_len == 11);
        CHECK(match_default_port->path_len == 24);
        CHECK(match_default_port->user_matched);
    }

    SECTION("Match with path prefix") {
        auto match_path_prefix =
            url_h.value()->MatchConfigKey("http://user@example.com/foo");
        REQUIRE(match_path_prefix);

        CHECK(match_path_prefix->matched);
        CHECK(match_path_prefix->host_len == 11);
        CHECK(match_path_prefix->path_len == 5);
        CHECK(match_path_prefix->user_matched);
    }

    SECTION("Match with path normalization") {
        auto match_path_normal = url_h.value()->MatchConfigKey(
            "http://user@example.com/./foo/boo/..");
        REQUIRE(match_path_normal);

        CHECK(match_path_normal->matched);
        CHECK(match_path_normal->host_len == 11);
        CHECK(match_path_normal->path_len == 5);
        CHECK(match_path_normal->user_matched);
    }

    SECTION("Match with wildcarded host") {
        auto match_wildcard_host = url_h.value()->MatchConfigKey(
            "http://user@*.com/foo/bar?query#fragment");
        REQUIRE(match_wildcard_host);

        CHECK(match_wildcard_host->matched);
        CHECK(match_wildcard_host->host_len == 5);
        CHECK(match_wildcard_host->path_len == 24);
        CHECK(match_wildcard_host->user_matched);
    }

    SECTION("Match with multiple wildcarded host") {
        auto match_wildcard_host = url_h.value()->MatchConfigKey(
            "http://user@*.*/foo/bar?query#fragment");
        REQUIRE(match_wildcard_host);

        CHECK(match_wildcard_host->matched);
        CHECK(match_wildcard_host->host_len == 3);
        CHECK(match_wildcard_host->path_len == 24);
        CHECK(match_wildcard_host->user_matched);
    }

    SECTION("Match fail with unparsable key") {
        auto match_fail_parse = url_h.value()->MatchConfigKey("192.0.2.1");
        REQUIRE(match_fail_parse);

        CHECK(not match_fail_parse->matched);
        CHECK(match_fail_parse->host_len == 0);
        CHECK(match_fail_parse->path_len == 0);
        CHECK(not match_fail_parse->user_matched);
    }

    SECTION("Match fail with wrong host") {
        auto match_fail_host = url_h.value()->MatchConfigKey(
            "http://user@example.org/foo/bar?query#fragment");
        REQUIRE(match_fail_host);

        CHECK(not match_fail_host->matched);
        CHECK(match_fail_host->host_len == 0);
        CHECK(match_fail_host->path_len == 0);
        CHECK(not match_fail_host->user_matched);
    }

    SECTION("Match fail with wrong port") {
        auto match_fail_port = url_h.value()->MatchConfigKey(
            "http://user@example.com:1234/foo/bar?query#fragment");
        REQUIRE(match_fail_port);

        CHECK(not match_fail_port->matched);
        CHECK(match_fail_port->host_len == 0);
        CHECK(match_fail_port->path_len == 0);
        CHECK(not match_fail_port->user_matched);
    }

    SECTION("Match fail with wrong path") {
        auto match_fail_path =
            url_h.value()->MatchConfigKey("http://user@example.com/foo/bar");
        REQUIRE(match_fail_path);

        CHECK(not match_fail_path->matched);
        CHECK(match_fail_path->host_len == 0);
        CHECK(match_fail_path->path_len == 0);
        CHECK(not match_fail_path->user_matched);
    }
}

TEST_CASE("Curl URL match no_proxy patterns", "[curl_url_match_no-proxy]") {
    auto url_h = CurlURLHandle::Create(
        "http://user@example.com:50000/foo/bar?query#fragment");
    REQUIRE(url_h);
    REQUIRE(*url_h);

    SECTION("Match with wildcard") {
        auto match_wildcard = url_h.value()->NoproxyStringMatches("*");
        REQUIRE(match_wildcard);
        CHECK(*match_wildcard);
    }

    SECTION("Match with host") {
        auto match_host = url_h.value()->NoproxyStringMatches("example.com");
        REQUIRE(match_host);
        CHECK(*match_host);
    }

    SECTION("Match with domain only") {
        auto match_domain = url_h.value()->NoproxyStringMatches("com");
        REQUIRE(match_domain);
        CHECK(*match_domain);
    }

    SECTION("Match with stripped leading dot") {
        auto match_host_leading_dot =
            url_h.value()->NoproxyStringMatches(".example.com");
        REQUIRE(match_host_leading_dot);
        CHECK(*match_host_leading_dot);
    }

    SECTION("Match with port") {
        auto match_port =
            url_h.value()->NoproxyStringMatches("example.com:50000");
        REQUIRE(match_port);
        CHECK(*match_port);
    }

    SECTION("Match from multiple patterns") {
        auto match_check_parse =
            url_h.value()->NoproxyStringMatches("fail, wrong   *");
        REQUIRE(match_check_parse);
        CHECK(*match_check_parse);
    }

    SECTION("Match fail with wrong patterns") {
        auto match_fail =
            url_h.value()->NoproxyStringMatches("fail, wrong :50000,example");
        REQUIRE(match_fail);
        CHECK(not *match_fail);
    }
}
