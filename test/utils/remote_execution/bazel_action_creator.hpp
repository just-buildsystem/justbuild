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

#ifndef INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_ACTION_CREATOR_HPP
#define INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_ACTION_CREATOR_HPP

#include <algorithm>  // std::transform, std::copy
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "google/protobuf/repeated_ptr_field.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/remote/remote_common.hpp"
#include "src/buildtool/common/remote/retry_config.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"
#include "test/utils/remote_execution/test_remote_config.hpp"

[[nodiscard]] static inline auto CreateAction(
    std::string const& instance_name,
    std::vector<std::string> const& args,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties,
    HashFunction hash_function) noexcept -> std::unique_ptr<bazel_re::Digest> {
    auto platform = std::make_unique<bazel_re::Platform>();
    for (auto const& [name, value] : properties) {
        bazel_re::Platform_Property property;
        property.set_name(name);
        property.set_value(value);
        *(platform->add_properties()) = property;
    }

    std::unordered_set<ArtifactBlob> blobs;

    bazel_re::Command cmd;
    cmd.set_allocated_platform(platform.release());
    std::copy(
        args.begin(), args.end(), pb::back_inserter(cmd.mutable_arguments()));

    std::transform(std::begin(env_vars),
                   std::end(env_vars),
                   pb::back_inserter(cmd.mutable_environment_variables()),
                   [](auto const& name_value) {
                       bazel_re::Command_EnvironmentVariable env_var_message;
                       env_var_message.set_name(name_value.first);
                       env_var_message.set_value(name_value.second);
                       return env_var_message;
                   });

    auto cmd_data = cmd.SerializeAsString();
    auto cmd_id = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, cmd_data);
    blobs.emplace(cmd_id, cmd_data, /*is_exec=*/false);

    bazel_re::Directory empty_dir;
    auto dir_data = empty_dir.SerializeAsString();
    auto dir_id = ArtifactDigestFactory::HashDataAs<ObjectType::Tree>(
        hash_function, dir_data);
    blobs.emplace(dir_id, dir_data, /*is_exec=*/false);

    bazel_re::Action action;
    (*action.mutable_command_digest()) = ArtifactDigestFactory::ToBazel(cmd_id);
    action.set_do_not_cache(false);
    (*action.mutable_input_root_digest()) =
        ArtifactDigestFactory::ToBazel(dir_id);

    auto action_data = action.SerializeAsString();
    auto action_id = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, action_data);
    blobs.emplace(action_id, action_data, /*is_exec=*/false);

    auto auth_config = TestAuthConfig::ReadFromEnvironment();
    if (not auth_config) {
        return nullptr;
    }

    auto remote_config = TestRemoteConfig::ReadFromEnvironment();
    if (not remote_config or not remote_config->remote_address) {
        return nullptr;
    }

    RetryConfig retry_config{};  // default retry config

    BazelCasClient cas_client(remote_config->remote_address->host,
                              remote_config->remote_address->port,
                              &*auth_config,
                              &retry_config);

    if (cas_client.BatchUpdateBlobs(instance_name, blobs) == blobs.size()) {
        return std::make_unique<bazel_re::Digest>(
            ArtifactDigestFactory::ToBazel(action_id));
    }
    return nullptr;
}

#endif  // INCLUDED_SRC_TEST_UTILS_REMOTE_EXECUTION_ACTION_CREATOR_HPP
