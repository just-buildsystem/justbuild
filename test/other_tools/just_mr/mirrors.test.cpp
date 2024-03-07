// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_all.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"

TEST_CASE("SortByHostname") {
    // setup inputs
    auto mirrors = std::vector<std::string>({"file://foo/bar",
                                             "https://keep.me/here",
                                             "https://example.com:420/foo bar",
                                             "./testing",
                                             "https://example.com:420/foo baz",
                                             "https://keep.me/second",
                                             "http://user@bar.baz/foobar"});
    auto hostnames =
        std::vector<std::string>({"bar.baz", "example.com", "bar.baz"});

    // compute ordered mirrors
    auto ordered = MirrorsUtils::SortByHostname(mirrors, hostnames);

    // compare with expected, Equals() honors order
    auto expected = std::vector<std::string>({"http://user@bar.baz/foobar",
                                              "https://example.com:420/foo bar",
                                              "https://example.com:420/foo baz",
                                              "file://foo/bar",
                                              "https://keep.me/here",
                                              "./testing",
                                              "https://keep.me/second"});
    CHECK_THAT(ordered, Catch::Matchers::Equals(expected));
}
