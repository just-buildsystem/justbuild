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

#include "src/utils/cpp/path_rebase.hpp"

#include <string>
#include <vector>

#include "catch2/catch_test_macros.hpp"

TEST_CASE("rebase", "[path_rebase]") {
    CHECK(RebasePathStringRelativeTo("work", "work/foo/bar") == "foo/bar");
    CHECK(RebasePathStringRelativeTo("work", "work/foo") == "foo");
    CHECK(RebasePathStringRelativeTo("work", "work") == ".");
    CHECK(RebasePathStringRelativeTo("work", "other/foo.txt") ==
          "../other/foo.txt");
    CHECK(RebasePathStringRelativeTo("work/foo", "foo.txt") == "../../foo.txt");
    CHECK(RebasePathStringRelativeTo("work/foo", "work/foo/bar") == "bar");
    CHECK(RebasePathStringRelativeTo("work/foo", "work/foo") == ".");
    CHECK(RebasePathStringRelativeTo("work/foo", "work") == "..");
}

TEST_CASE("no change", "[path_rebase]") {
    CHECK(RebasePathStringRelativeTo("", "work/foo/bar") == "work/foo/bar");
    CHECK(RebasePathStringRelativeTo(".", "work/foo/bar") == "work/foo/bar");
    CHECK(RebasePathStringRelativeTo("", ".") == ".");
    CHECK(RebasePathStringRelativeTo("", "") == ".");
}

TEST_CASE("vector rebse", "[path_rebase]") {
    std::vector<std::string> input{
        "work/foo.txt", "work/bar/baz.txt", "other/out.txt"};
    auto output = RebasePathStringsRelativeTo("work", input);
    REQUIRE(output.size() == 3);
    CHECK(output[0] == "foo.txt");
    CHECK(output[1] == "bar/baz.txt");
    CHECK(output[2] == "../other/out.txt");
}
