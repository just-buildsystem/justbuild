// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/execution_api/execution_service/execution_server.hpp"

#include <algorithm>
#include <compare>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

// Don't include "proto"
// IWYU pragma: no_include "google/longrunning/operations.pb.h"
// IWYU pragma: no_include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_tostring.hpp"
#include "catch2/generators/catch_generators_all.hpp"
#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/execution_service/cas_server.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/context.hpp"
#include "src/buildtool/execution_api/local/local_api.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_capabilities_client.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"

namespace {

auto const kV20 = Capabilities::Version{.major = 2, .minor = 0, .patch = 0};
auto const kV21 = Capabilities::Version{.major = 2, .minor = 1, .patch = 0};

// Class to obtain a valid pointer to internal ServerWriter<...::Operation>
class MockServerWriter final
    : public ::grpc::ServerWriterInterface<::google::longrunning::Operation> {
  public:
    MockServerWriter() = default;
    [[nodiscard]] auto Get()
        -> ::grpc::ServerWriter<::google::longrunning::Operation>* {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<
            ::grpc::ServerWriter<::google::longrunning::Operation>*>(this);
    }

    // stub implementations
    void SendInitialMetadata() override {}
    using ::grpc::internal::WriterInterface<
        google::longrunning::Operation>::Write;
    auto Write(google::longrunning::Operation const& /*msg*/,
               grpc::WriteOptions /*options*/) -> bool override {
        return true;
    }

  private:
    MockServerWriter(grpc::internal::Call* /*call*/,
                     grpc::ServerContext* /*ctx*/) {}
};

template <ObjectType kType>
[[nodiscard]] auto Upload(
    gsl::not_null<bazel_re::ContentAddressableStorage::Service*> const&
        cas_server,
    std::string const& instance_name,
    gsl::not_null<const StorageConfig*> const& storage_config,
    std::string const& content) noexcept -> std::optional<bazel_re::Digest> {
    auto digest = BazelDigestFactory::HashDataAs<kType>(
        storage_config->hash_function, content);
    auto request = bazel_re::BatchUpdateBlobsRequest{};
    request.set_instance_name(instance_name);
    auto* req = request.add_requests();
    req->mutable_digest()->CopyFrom(digest);
    req->set_data(content);
    auto response = bazel_re::BatchUpdateBlobsResponse{};
    if (cas_server->BatchUpdateBlobs(nullptr, &request, &response).ok()) {
        return digest;
    }
    return std::nullopt;
}

[[nodiscard]] auto CreateEmptyTree(
    gsl::not_null<bazel_re::ContentAddressableStorage::Service*> const&
        cas_server,
    gsl::not_null<const StorageConfig*> const& storage_config,
    std::string const& instance_name) noexcept {
    if (ProtocolTraits::IsNative(TestHashType::ReadFromEnvironment())) {
        auto empty_entries = GitRepo::tree_entries_t{};
        auto empty_tree = GitRepo::CreateShallowTree(empty_entries);
        REQUIRE(empty_tree);
        auto digest = Upload<ObjectType::Tree>(
            cas_server, instance_name, storage_config, empty_tree->second);
        REQUIRE(digest);
        return *digest;
    }
    auto digest =
        Upload<ObjectType::File>(cas_server,
                                 instance_name,
                                 storage_config,
                                 bazel_re::Directory{}.SerializeAsString());
    REQUIRE(digest);
    return *digest;
}

[[nodiscard]] auto Execute(
    gsl::not_null<bazel_re::ContentAddressableStorage::Service*> const&
        cas_server,
    gsl::not_null<bazel_re::Execution::Service*> const& exec_server,
    gsl::not_null<const StorageConfig*> const& storage_config,
    std::string const& instance_name,
    bazel_re::Digest const& root_digest,
    std::string const& cwd,
    std::vector<std::string> const& argv,
    std::vector<std::string> output_files,
    std::vector<std::string> output_dirs,
    std::map<std::string, std::string> const& env,
    std::map<std::string, std::string> const& properties,
    Capabilities::Version const& version) noexcept
    -> std::optional<ArtifactDigest> {
    auto get_platform = [&properties]() {
        auto platform = std::make_unique<bazel_re::Platform>();
        std::transform(properties.begin(),
                       properties.end(),
                       pb::back_inserter(platform->mutable_properties()),
                       [](auto prop) {
                           auto out = bazel_re::Platform_Property{};
                           out.set_name(prop.first);
                           out.set_value(prop.second);
                           return out;
                       });
        return platform.release();
    };

    // create command
    auto cmd = bazel_re::Command{};
    std::copy(
        argv.begin(), argv.end(), pb::back_inserter(cmd.mutable_arguments()));
    cmd.set_working_directory(cwd);
    std::transform(env.begin(),
                   env.end(),
                   pb::back_inserter(cmd.mutable_environment_variables()),
                   [](auto const& name_val) {
                       auto var = bazel_re::Command_EnvironmentVariable{};
                       var.set_name(name_val.first);
                       var.set_value(name_val.second);
                       return var;
                   });
    if (version >= kV21) {
        auto paths = std::vector<std::string>{};
        paths.reserve(output_files.size() + output_dirs.size());
        paths.insert(paths.end(),
                     std::move_iterator{output_files.begin()},
                     std::move_iterator{output_files.end()});
        paths.insert(paths.end(),
                     std::move_iterator{output_dirs.begin()},
                     std::move_iterator{output_dirs.end()});
        std::sort(paths.begin(), paths.end());
        std::copy(paths.begin(),
                  paths.end(),
                  pb::back_inserter(cmd.mutable_output_paths()));
    }
    else {
        std::sort(output_files.begin(), output_files.end());
        std::sort(output_dirs.begin(), output_dirs.end());
        std::copy(output_files.begin(),
                  output_files.end(),
                  pb::back_inserter(cmd.mutable_output_files()));
        std::copy(output_dirs.begin(),
                  output_dirs.end(),
                  pb::back_inserter(cmd.mutable_output_directories()));
    }
    cmd.set_allocated_platform(get_platform());
    auto cmd_digest = Upload<ObjectType::File>(
        cas_server, instance_name, storage_config, cmd.SerializeAsString());
    REQUIRE(cmd_digest);

    // create action
    auto action = bazel_re::Action{};
    action.mutable_command_digest()->CopyFrom(*cmd_digest);
    action.mutable_input_root_digest()->CopyFrom(root_digest);
    auto action_digest = Upload<ObjectType::File>(
        cas_server, instance_name, storage_config, action.SerializeAsString());
    REQUIRE(action_digest);

    // create execute request
    auto request = bazel_re::ExecuteRequest{};
    request.set_instance_name(instance_name);
    request.mutable_action_digest()->CopyFrom(*action_digest);

    // mock server-internal execute call
    auto writer = MockServerWriter{};
    auto status = exec_server->Execute(nullptr, &request, writer.Get());
    if (status.ok()) {
        if (auto just_digest = ArtifactDigestFactory::FromBazel(
                storage_config->hash_function.GetType(), *action_digest)) {
            return *std::move(just_digest);
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto ToString(Capabilities::Version const& version)
    -> std::string {
    return fmt::format("{}.{}.{}", version.major, version.minor, version.patch);
}

}  // namespace

TEST_CASE("Execution Service: Test supported API versions",
          "[execution_service]") {
    auto const storage_config = TestStorageConfig::Create();
    auto const storage = Storage::Create(&storage_config.Get());
    LocalExecutionConfig const local_exec_config{};

    // pack the local context instances to be passed
    LocalContext const local_context{.exec_config = &local_exec_config,
                                     .storage_config = &storage_config.Get(),
                                     .storage = &storage};

    auto local_api = LocalApi{&local_context};
    auto exec_server =
        ExecutionServiceImpl{&local_context, &local_api, std::nullopt};

    auto cas_server = CASServiceImpl{&local_context};
    auto instance_name = std::string{"remote-execution"};

    auto root_digest =
        CreateEmptyTree(&cas_server, &storage_config.Get(), instance_name);

    auto env = std::map<std::string, std::string>{};
    if (auto const* path_var = std::getenv("PATH")) {
        // server executes locally, make sure it knows about PATH from TEST_ENV
        env.emplace("PATH", path_var);
    }

    auto version = GENERATE(kV20, kV21);

    DYNAMIC_SECTION("Pretend being a client using RBEv" << ToString(version)) {
        auto action_digest =
            Execute(&cas_server,
                    &exec_server,
                    &storage_config.Get(),
                    instance_name,
                    root_digest,
                    "",
                    {"/bin/sh",
                     "-c",
                     "set -e; touch foo; ln -s none fox; "
                     "mkdir -p bar; rm -rf bat; ln -s none bat"},
                    {"foo", "fox"},
                    {"bar", "bat"},
                    env,
                    {},
                    version);
        REQUIRE(action_digest);

        auto result = storage.ActionCache().CachedResult(*action_digest);
        REQUIRE(result);

        // check output files and directories
        CHECK_FALSE(result->output_files().empty());
        CHECK_FALSE(result->output_directories().empty());
        if (not result->output_files().empty()) {
            CHECK(result->output_files().begin()->path() == "foo");
        }
        if (not result->output_directories().empty()) {
            CHECK(result->output_directories().begin()->path() == "bar");
        }

        // check output symlinks
        if (version >= kV21) {
            // starting from RBEv2.1, output_symlinks must be filled
            CHECK(result->output_symlinks_size() == 2);
            if (result->output_symlinks_size() == 2) {
                auto paths = std::unordered_set<std::string>{};
                paths.reserve(2);
                std::transform(result->output_symlinks().begin(),
                               result->output_symlinks().end(),
                               std::inserter(paths, paths.end()),
                               [](auto const& link) { return link.path(); });
                CHECK(paths.contains("fox"));
                CHECK(paths.contains("bat"));
            }

            // separated file/dir symlinks may be reported additionally
            if (not result->output_file_symlinks().empty()) {
                CHECK(result->output_file_symlinks().begin()->path() == "fox");
            }
            if (not result->output_directory_symlinks().empty()) {
                CHECK(result->output_directory_symlinks().begin()->path() ==
                      "bat");
            }
        }
        else {
            // in legacy mode, output_symlinks must not be set...
            CHECK(result->output_symlinks().empty());
            // ... instead, file/dir symlinks must be reported separately
            CHECK_FALSE(result->output_file_symlinks().empty());
            CHECK_FALSE(result->output_directory_symlinks().empty());
            if (not result->output_file_symlinks().empty()) {
                CHECK(result->output_file_symlinks().begin()->path() == "fox");
            }
            if (not result->output_directory_symlinks().empty()) {
                CHECK(result->output_directory_symlinks().begin()->path() ==
                      "bat");
            }
        }
    }
}
