// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/utils/cpp/path.hpp"

#include <filesystem>
#include <string>

#include "catch2/catch_test_macros.hpp"

TEST_CASE("normalization", "[path]") {
    CHECK(ToNormalPath(std::filesystem::path{""}) ==
          ToNormalPath(std::filesystem::path{"."}));
    CHECK(ToNormalPath(std::filesystem::path{""}).string() == ".");
    CHECK(ToNormalPath(std::filesystem::path{"."}).string() == ".");

    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/.."}).string() == "foo");
    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/../"}).string() == "foo");
    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/../baz"}).string() ==
          "foo/baz");
    CHECK(ToNormalPath(std::filesystem::path{"./foo/bar"}).string() ==
          "foo/bar");
    CHECK(ToNormalPath(std::filesystem::path{"foo/.."}).string() == ".");
    CHECK(ToNormalPath(std::filesystem::path{"./foo/.."}).string() == ".");
}

TEST_CASE("non-upwards condition", "[path]") {
    CHECK_FALSE(PathIsNonUpwards("/foo"));    // absolute path
    CHECK(PathIsNonUpwards("foo"));           // relative non-upwards
    CHECK_FALSE(PathIsNonUpwards("../foo"));  // relative not non-upwards
    CHECK_FALSE(PathIsNonUpwards(
        "foo/../bar/../../foo"));  // relative with non-upwards indirection
}

TEST_CASE("confined upwards condition", "[path]") {
    CHECK_FALSE(PathIsConfined("/foo", "dummy"));  // absolute path
    CHECK(PathIsConfined("foo", "dummy"));         // relative non-upwards
    CHECK(PathIsConfined("../foo", "dummy/bar"));  // upwards, but confined
    CHECK_FALSE(PathIsConfined("foo/../bar/../../../foo",
                               "dummy"));  // upwards, not confined
}
