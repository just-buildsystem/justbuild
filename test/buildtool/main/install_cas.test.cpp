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

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/main/install_cas.hpp"

TEST_CASE("ObjectInfoFromLiberalString", "[artifact]") {
    auto expected = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]");
    auto expected_as_tree = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:0:t]");

    CHECK(ObjectInfoFromLiberalString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:file") == expected);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      ":11:notavalidletter") == expected);

    // Without size, which is not honored in equality
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:") == expected);
    // Syntactically invalid size should be ignored
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:xyz") == expected);

    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      "::t") == expected_as_tree);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      "::tree") == expected_as_tree);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      ":xyz:t") == expected_as_tree);
}
