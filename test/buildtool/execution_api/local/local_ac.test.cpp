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

#include <string>

#include "catch2/catch.hpp"
#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/execution_api/local/local_ac.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "test/utils/hermeticity/local.hpp"

[[nodiscard]] static auto RunDummyExecution(gsl::not_null<LocalAC*> const& ac,
                                            bazel_re::Digest const& action_id,
                                            std::string const& seed) -> bool;

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAC: Single action, single result",
                 "[execution_api]") {
    LocalCAS cas{};
    LocalAC ac{&cas};

    auto action_id = ArtifactDigest::Create<ObjectType::File>("action");
    CHECK(not ac.CachedResult(action_id));

    CHECK(RunDummyExecution(&ac, action_id, "result"));
    auto ac_result = ac.CachedResult(action_id);
    CHECK(ac_result);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAC: Two different actions, two different results",
                 "[execution_api]") {
    LocalCAS cas{};
    LocalAC ac{&cas};

    auto action_id1 = ArtifactDigest::Create<ObjectType::File>("action1");
    auto action_id2 = ArtifactDigest::Create<ObjectType::File>("action2");
    CHECK(not ac.CachedResult(action_id1));
    CHECK(not ac.CachedResult(action_id2));

    std::string result_content1{};
    std::string result_content2{};

    CHECK(RunDummyExecution(&ac, action_id1, "result1"));
    auto ac_result1 = ac.CachedResult(action_id1);
    REQUIRE(ac_result1);
    CHECK(ac_result1->SerializeToString(&result_content1));

    CHECK(RunDummyExecution(&ac, action_id2, "result2"));
    auto ac_result2 = ac.CachedResult(action_id2);
    REQUIRE(ac_result2);
    CHECK(ac_result2->SerializeToString(&result_content2));

    // check different actions, different result
    CHECK(action_id1.hash() != action_id2.hash());
    CHECK(result_content1 != result_content2);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAC: Two different actions, same two results",
                 "[execution_api]") {
    LocalCAS cas{};
    LocalAC ac{&cas};

    auto action_id1 = ArtifactDigest::Create<ObjectType::File>("action1");
    auto action_id2 = ArtifactDigest::Create<ObjectType::File>("action2");
    CHECK(not ac.CachedResult(action_id1));
    CHECK(not ac.CachedResult(action_id2));

    std::string result_content1{};
    std::string result_content2{};

    CHECK(RunDummyExecution(&ac, action_id1, "same result"));
    auto ac_result1 = ac.CachedResult(action_id1);
    REQUIRE(ac_result1);
    CHECK(ac_result1->SerializeToString(&result_content1));

    CHECK(RunDummyExecution(&ac, action_id2, "same result"));
    auto ac_result2 = ac.CachedResult(action_id2);
    REQUIRE(ac_result2);
    CHECK(ac_result2->SerializeToString(&result_content2));

    // check different actions, but same result
    CHECK(action_id1.hash() != action_id2.hash());
    CHECK(result_content1 == result_content2);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalAC: Same two actions, two differnet results",
                 "[execution_api]") {
    LocalCAS cas{};
    LocalAC ac{&cas};

    auto action_id = ArtifactDigest::Create<ObjectType::File>("same action");
    CHECK(not ac.CachedResult(action_id));

    std::string result_content1{};
    std::string result_content2{};

    CHECK(RunDummyExecution(&ac, action_id, "result1"));
    auto ac_result1 = ac.CachedResult(action_id);
    REQUIRE(ac_result1);
    CHECK(ac_result1->SerializeToString(&result_content1));

    CHECK(RunDummyExecution(&ac, action_id, "result2"));  // updated
    auto ac_result2 = ac.CachedResult(action_id);
    REQUIRE(ac_result2);
    CHECK(ac_result2->SerializeToString(&result_content2));

    // check same actions, different cached result
    CHECK(result_content1 != result_content2);
}

auto RunDummyExecution(gsl::not_null<LocalAC*> const& ac,
                       bazel_re::Digest const& action_id,
                       std::string const& seed) -> bool {
    bazel_re::ActionResult result{};
    *result.add_output_files() = [&]() {
        bazel_re::OutputFile out{};
        out.set_path(seed);
        return out;
    }();
    return ac->StoreResult(action_id, result);
}
