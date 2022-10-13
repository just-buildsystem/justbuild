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
#include "src/buildtool/execution_api/remote/bazel/bazel_ac_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "test/utils/remote_execution/bazel_action_creator.hpp"
#include "test/utils/test_env.hpp"

auto CreateActionCacheEntry(BazelAcClient* ac_client,
                            std::string const& instance_name,
                            bazel_re::Digest const& action_id,
                            std::string const& output) {
    bazel_re::ActionResult result{};
    result.set_stdout_raw(output);
    REQUIRE(ac_client->UpdateActionResult(instance_name, action_id, result, 1));
}

// IMPORTANT: we are hiding this test case because the version of buildbarn we
// are currently using does not allow us to upload the action to the AC
// directly. The test was not failing due to a similar action being updated by
// another test (and lack of hermeticity), so it is better to disable it than to
// have it fail if we change that other test or reset the buildbarn server and
// run only the current test case. See issue#30 in
// https://rnd-gitlab-eu-c.huawei.com/germany-research-center/intelligent-cloud-technologies-laboratory/9424510-devcloud-build-tool-technology-project-de/-/issues/30
TEST_CASE("Bazel internals: AC Client", "[!hide][execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();

    BazelAcClient ac_client(info->host, info->port);

    std::string instance_name{"remote-execution"};
    std::string content("test");
    auto test_digest = ArtifactDigest::Create<ObjectType::File>(content);

    auto action_id = CreateAction(instance_name,
                                  {"echo", "-n", content},
                                  {},
                                  RemoteExecutionConfig::PlatformProperties());
    REQUIRE(action_id);

    // TODO(investigate): Upload fails due to permission issues. The BuildBarn
    // revision we are currently using seems to ignore the
    // 'allowAcUpdatesForInstances' setting.
    CreateActionCacheEntry(&ac_client, instance_name, *action_id, content);

    auto ac_result =
        ac_client.GetActionResult(instance_name, *action_id, true, true, {});
    REQUIRE(ac_result);
    CHECK(std::equal_to<bazel_re::Digest>{}(ac_result->stdout_digest(),
                                            test_digest));
}
