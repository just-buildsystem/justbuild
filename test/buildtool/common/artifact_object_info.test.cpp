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
#include "nlohmann/json.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/object_type.hpp"

TEST_CASE("Consistency check for serialization and de-serialization",
          "[object_info]") {

    auto empty_blob = Artifact::ObjectInfo{
        .digest =
            ArtifactDigest{
                "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391", 0, false},
        .type = ObjectType::File};

    auto x = empty_blob.ToJson().dump();

    auto read = Artifact::ObjectInfo::FromJson(nlohmann::json::parse(x));
    REQUIRE(read.has_value());
    CHECK(*read == empty_blob);
}
