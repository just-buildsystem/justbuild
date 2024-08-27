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

#include "src/other_tools/just_mr/rc_merge.hpp"

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"

TEST_CASE("Simple field") {
    auto conf = Configuration{Expression::FromJson(R"(
         { "log limit": 4
         , "git": {"root": "system", "path": "usr/bin/git"}
         })"_json)};
    auto delta = Configuration{Expression::FromJson(R"(
         { "log limit": 5
         })"_json)};

    auto merged = MergeMRRC(conf, delta);
    CHECK(merged["log limit"] == Expression::FromJson("5"_json));
    CHECK(merged["git"] ==
          Expression::FromJson(
              R"({"root": "system", "path": "usr/bin/git"})"_json));
}

TEST_CASE("accumulating") {
    auto conf = Configuration{Expression::FromJson(R"(
      {"distdirs": [{"root": "home", "path": ".distfiles"}]}
    )"_json)};
    auto delta = Configuration{Expression::FromJson(R"(
      {"distdirs": [{"root": "workspace", "path": "third_party"}]}
    )"_json)};

    auto merged = MergeMRRC(conf, delta);
    CHECK(merged["distdirs"] == Expression::FromJson(R"(
      [ {"root": "workspace", "path": "third_party"}
      , {"root": "home", "path": ".distfiles"}
      ]
    )"_json));
}

TEST_CASE("local-merge") {
    auto conf = Configuration{Expression::FromJson(R"(
      {"just args": {"build": ["-J", "8"], "install": ["-J", "8", "--remember"]}}
    )"_json)};
    auto delta = Configuration{Expression::FromJson(R"(
      {"just args": {"build": ["-J", "128"], "install-cas": ["--remember"]}}
    )"_json)};

    auto merged = MergeMRRC(conf, delta);
    CHECK(merged["just args"] == Expression::FromJson(R"(
      { "build": ["-J", "128"]
      , "install-cas": ["--remember"]
      , "install": ["-J", "8", "--remember"]
      }
    )"_json));
}
