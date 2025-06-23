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

#ifndef INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_API_COMMON_API_TEST_HPP
#define INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_API_COMMON_API_TEST_HPP

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_blob.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_action.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/local_response.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

using ApiFactory = std::function<IExecutionApi::Ptr()>;
using ExecProps = std::map<std::string, std::string>;

[[nodiscard]] static inline auto CreateLocalExecConfig() noexcept
    -> LocalExecutionConfig {
    std::vector<std::string> launcher{"env"};
    auto* env_path = std::getenv("PATH");
    if (env_path != nullptr) {
        launcher.emplace_back(std::string{"PATH="} + std::string{env_path});
    }
    else {
        launcher.emplace_back("PATH=/bin:/usr/bin");
    }
    LocalExecutionConfig::Builder builder;
    if (auto config = builder.SetLauncher(std::move(launcher)).Build()) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, "Failure setting the local launcher.");
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] static inline auto GetTestDir(std::string const& test_name)
    -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return std::filesystem::path{tmp_dir} / test_name;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/execution_api" / test_name;
}

[[nodiscard]] static inline auto TestNoInputNoOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");

    auto api = api_factory();

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"echo", "-n", test_content},
                                    "",
                                    {},
                                    {},
                                    {},
                                    props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->HasStdOut());
        CHECK(response->StdOut() == test_content);

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto response = action->Execute();
                REQUIRE(response);

                // verify result
                CHECK(response->HasStdOut());
                CHECK(response->StdOut() == test_content);
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->HasStdOut());
        CHECK(response->StdOut() == test_content);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->HasStdOut());
            CHECK(response->StdOut() == test_content);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestNoInputCreateOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto test_digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, test_content);

    std::string output_path{"output_file"};

    auto api = api_factory();

    auto action = api->CreateAction(
        *api->UploadTree({}),
        {"/bin/sh",
         "-c",
         "set -e\necho -n " + test_content + " > " + output_path},
        "",
        {output_path},
        {},
        {},
        props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest == test_digest);

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto const response = action->Execute();
                REQUIRE(response);

                // verify result
                auto const artifacts = response->Artifacts();
                REQUIRE(artifacts.has_value());
                REQUIRE(artifacts.value()->contains(output_path));
                CHECK(artifacts.value()->at(output_path).digest == test_digest);
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest == test_digest);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto const response = action->Execute();
            REQUIRE(response);

            // verify result
            auto const artifacts = response->Artifacts();
            REQUIRE(artifacts.value()->contains(output_path));
            CHECK(artifacts.value()->at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestOneInputCopiedToOutput(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    std::string test_content("test");

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto const test_blob =
        ArtifactBlob::FromMemory(hash_function, ObjectType::File, test_content);
    REQUIRE(test_blob.has_value());

    auto input_artifact_opt = ArtifactDescription::CreateKnown(
                                  test_blob->GetDigest(), ObjectType::File)
                                  .ToArtifact();
    auto input_artifact =
        DependencyGraph::ArtifactNode{std::move(input_artifact_opt)};

    std::string input_path{"dir/subdir/input"};
    std::string output_path{"output_file"};

    auto api = api_factory();
    CHECK(api->Upload({*test_blob}, false));

    auto action =
        api->CreateAction(*api->UploadTree({{input_path, &input_artifact}}),
                          {"cp", input_path, output_path},
                          "",
                          {output_path},
                          {},
                          {},
                          props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest ==
              test_blob->GetDigest());

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto const response = action->Execute();
                REQUIRE(response);

                // verify result
                auto const artifacts = response->Artifacts();
                REQUIRE(artifacts.has_value());
                REQUIRE(artifacts.value()->contains(output_path));
                CHECK(artifacts.value()->at(output_path).digest ==
                      test_blob->GetDigest());
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest ==
              test_blob->GetDigest());
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto const response = action->Execute();
            REQUIRE(response);

            // verify result
            auto const artifacts = response->Artifacts();
            REQUIRE(artifacts.has_value());
            REQUIRE(artifacts.value()->contains(output_path));
            CHECK(artifacts.value()->at(output_path).digest ==
                  test_blob->GetDigest());
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestNonZeroExitCodeCreateOutput(
    ApiFactory const& api_factory,
    ExecProps const& props) {
    std::string test_content("test");

    HashFunction const hash_function{TestHashType::ReadFromEnvironment()};

    auto test_digest = ArtifactDigestFactory::HashDataAs<ObjectType::File>(
        hash_function, test_content);

    std::string output_path{"output_file"};

    auto api = api_factory();

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh",
                                     "-c",
                                     "set -e\necho -n " + test_content + " > " +
                                         output_path + "\nexit 1\n"},
                                    "",
                                    {output_path},
                                    {},
                                    {},
                                    props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->ExitCode() == 1);
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest == test_digest);

        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify that non-zero actions are rerun") {
            // run execution
            auto const response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->ExitCode() == 1);
            auto const artifacts = response->Artifacts();
            REQUIRE(artifacts.has_value());
            REQUIRE(artifacts.value()->contains(output_path));
            CHECK(artifacts.value()->at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        CHECK(response->ExitCode() == 1);
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(artifacts.value()->at(output_path).digest == test_digest);
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify non-zero actions are not cached") {
            // run execution
            auto response = action->Execute();
            REQUIRE(response);

            // verify result
            CHECK(response->ExitCode() == 1);
            auto const artifacts = response->Artifacts();
            REQUIRE(artifacts.has_value());
            REQUIRE(artifacts.value()->contains(output_path));
            CHECK(artifacts.value()->at(output_path).digest == test_digest);
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestRetrieveTwoIdenticalTreesToPath(
    ApiFactory const& api_factory,
    ExecProps const& props,
    std::string const& test_name,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto foo_path = std::filesystem::path{"foo"} / "baz";
    auto bar_path = std::filesystem::path{"bar"} / "baz";

    auto make_cmd = [&](std::string const& out_dir) {
        return fmt::format(
            "set -e\nmkdir -p {0}/{1} {0}/{2}\n"
            "echo -n baz > {0}/{3}\necho -n baz > {0}/{4}",
            out_dir,
            foo_path.parent_path().string(),
            bar_path.parent_path().string(),
            foo_path.string(),
            bar_path.string());
    };

    auto* path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (path != nullptr) {
        env.emplace("PATH", path);
    }

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh", "-c", make_cmd("root")},
                                    "",
                                    {},
                                    {"root"},
                                    env,
                                    props);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run execution
    auto const response = action->Execute();
    REQUIRE(response);

    // verify result
    CHECK(response->ExitCode() == 0);

    if (is_hermetic) {
        CHECK_FALSE(response->IsCached());
    }

    auto const artifacts = response->Artifacts();
    REQUIRE(artifacts.has_value());
    REQUIRE_FALSE(artifacts.value()->empty());

    auto info = artifacts.value()->begin()->second;

    SECTION("retrieve via same API object") {
        auto out_path = GetTestDir(test_name) / "out1";
        CHECK(api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadFile(out_path / bar_path));
    }

    SECTION("retrive from new API object but same endpoint") {
        auto second_api = api_factory();
        auto out_path = GetTestDir(test_name) / "out2";
        CHECK(second_api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadFile(out_path / bar_path));
    }
}

[[nodiscard]] static inline auto
TestRetrieveFileAndSymlinkWithSameContentToPath(ApiFactory const& api_factory,
                                                ExecProps const& props,
                                                std::string const& test_name,
                                                bool is_hermetic = false) {
    auto api = api_factory();

    auto foo_path = std::filesystem::path{"foo"} / "baz";  // file
    auto bar_path = std::filesystem::path{"bar"} / "baz";  // symlink

    auto make_cmd = [&](std::string const& out_dir) {
        return fmt::format(
            "set -e\nmkdir -p {0}/{1} {0}/{2}\n"
            "echo -n baz > {0}/{3}\nln -s baz {0}/{4}",
            out_dir,
            foo_path.parent_path().string(),
            bar_path.parent_path().string(),
            foo_path.string(),
            bar_path.string());
    };

    auto* path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (path != nullptr) {
        env.emplace("PATH", path);
    }

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh", "-c", make_cmd("root")},
                                    "",
                                    {},
                                    {"root"},
                                    env,
                                    props);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run execution
    auto const response = action->Execute();
    REQUIRE(response);

    // verify result
    CHECK(response->ExitCode() == 0);

    if (is_hermetic) {
        CHECK_FALSE(response->IsCached());
    }

    auto const artifacts = response->Artifacts();
    REQUIRE(artifacts.has_value());
    REQUIRE_FALSE(artifacts.value()->empty());

    auto info = artifacts.value()->begin()->second;

    SECTION("retrieve via same API object") {
        auto out_path = GetTestDir(test_name) / "out1";
        CHECK(api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadSymlink(out_path / bar_path));
    }

    SECTION("retrive from new API object but same endpoint") {
        auto second_api = api_factory();
        auto out_path = GetTestDir(test_name) / "out2";
        CHECK(second_api->RetrieveToPaths({info}, {out_path}));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(out_path / bar_path));
        CHECK(FileSystemManager::ReadFile(out_path / foo_path) ==
              FileSystemManager::ReadSymlink(out_path / bar_path));
    }
}

[[nodiscard]] static inline auto TestRetrieveMixedBlobsAndTrees(
    ApiFactory const& api_factory,
    ExecProps const& props,
    std::string const& test_name,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto foo_path = std::filesystem::path{"foo"};
    auto bar_path = std::filesystem::path{"subdir"} / "bar";
    auto link_path = std::filesystem::path{"sym"};

    auto cmd = fmt::format("set -e\nmkdir -p {}\ntouch {} {}\nln -s dummy {}",
                           bar_path.parent_path().string(),
                           bar_path.string(),
                           foo_path.string(),
                           link_path.string());

    auto* path = std::getenv("PATH");
    std::map<std::string, std::string> env{};
    if (path != nullptr) {
        env.emplace("PATH", path);
    }

    auto action = api->CreateAction(*api->UploadTree({}),
                                    {"/bin/sh", "-c", cmd},
                                    "",
                                    {foo_path.string(), link_path.string()},
                                    {bar_path.parent_path().string()},
                                    env,
                                    props);

    action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

    // run execution
    auto const response = action->Execute();
    REQUIRE(response);

    // verify result
    CHECK(response->ExitCode() == 0);

    if (is_hermetic) {
        CHECK_FALSE(response->IsCached());
    }

    auto const artifacts = response->Artifacts();
    REQUIRE(artifacts.has_value());
    REQUIRE_FALSE(artifacts.value()->empty());

    std::vector<std::filesystem::path> paths{};
    std::vector<Artifact::ObjectInfo> infos{};

    SECTION("retrieve via same API object") {
        auto out_path = GetTestDir(test_name) / "out1";
        std::for_each(artifacts.value()->begin(),
                      artifacts.value()->end(),
                      [&out_path, &paths, &infos](auto const& entry) {
                          paths.emplace_back(out_path / entry.first);
                          infos.emplace_back(entry.second);
                      });
        CHECK(api->RetrieveToPaths(infos, paths));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(out_path / link_path));
    }

    SECTION("retrieve from new API object but same endpoint") {
        auto second_api = api_factory();
        auto out_path = GetTestDir(test_name) / "out2";
        std::for_each(artifacts.value()->begin(),
                      artifacts.value()->end(),
                      [&out_path, &paths, &infos](auto const& entry) {
                          paths.emplace_back(out_path / entry.first);
                          infos.emplace_back(entry.second);
                      });
        CHECK(second_api->RetrieveToPaths(infos, paths));
        CHECK(FileSystemManager::IsFile(out_path / foo_path));
        CHECK(FileSystemManager::IsFile(out_path / bar_path));
        CHECK(FileSystemManager::IsNonUpwardsSymlink(out_path / link_path));
    }
}

[[nodiscard]] static inline auto TestCreateDirPriorToExecution(
    ApiFactory const& api_factory,
    ExecProps const& props,
    bool is_hermetic = false) {
    auto api = api_factory();

    auto output_path = std::filesystem::path{"foo/bar/baz"};

    auto action =
        api->CreateAction(*api->UploadTree({}),
                          {"/bin/sh",
                           "-c",
                           fmt::format("set -e\n [ -d {} ]\n mkdir -p {}",
                                       output_path.parent_path().string(),
                                       output_path.string())},
                          "",
                          {},
                          {output_path},
                          {},
                          props);

    SECTION("Cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::CacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(IsTreeObject(artifacts.value()->at(output_path).type));

        if (is_hermetic) {
            CHECK(not response->IsCached());

            SECTION("Rerun execution to verify caching") {
                // run execution
                auto const response = action->Execute();
                REQUIRE(response);

                // verify result
                auto const artifacts = response->Artifacts();
                REQUIRE(artifacts.has_value());
                REQUIRE(artifacts.value()->contains(output_path));
                CHECK(IsTreeObject(artifacts.value()->at(output_path).type));
                CHECK(response->IsCached());
            }
        }
    }

    SECTION("Do not cache execution result in action cache") {
        action->SetCacheFlag(IExecutionAction::CacheFlag::DoNotCacheOutput);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains(output_path));
        CHECK(IsTreeObject(artifacts.value()->at(output_path).type));
        CHECK(not response->IsCached());

        SECTION("Rerun execution to verify caching") {
            // run execution
            auto const response = action->Execute();
            REQUIRE(response);

            // verify result
            auto const artifacts = response->Artifacts();
            REQUIRE(artifacts.has_value());
            REQUIRE(artifacts.value()->contains(output_path));
            CHECK(IsTreeObject(artifacts.value()->at(output_path).type));
            CHECK(not response->IsCached());
        }
    }
}

[[nodiscard]] static inline auto TestSymlinkCollection(
    ApiFactory const& api_factory,
    ExecProps const& props) {
    auto api = api_factory();

    SECTION("dangling") {
        auto action = api->CreateAction(
            *api->UploadTree({}),
            {"/bin/sh",
             "-c",
             "set -e; "
             "ln -s none foo; "
             "rm -rf bar; ln -s none bar; "
             "mkdir -p baz; ln -s none baz/foo; ln -s none baz/bar"},
            "",
            {"foo"},
            {"bar", "baz"},
            {},
            props);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());
        REQUIRE(artifacts.value()->contains("foo"));
        CHECK(IsSymlinkObject(artifacts.value()->at("foo").type));
        REQUIRE(artifacts.value()->contains("bar"));
        CHECK(IsSymlinkObject(artifacts.value()->at("bar").type));
        REQUIRE(artifacts.value()->contains("baz"));
        CHECK(IsTreeObject(artifacts.value()->at("baz").type));

        // check if bar was correctly detected as directory symlink
        auto* local_response = dynamic_cast<LocalResponse*>(response.get());
        if (local_response != nullptr) {
            auto dir_symlinks = local_response->DirectorySymlinks();
            REQUIRE(dir_symlinks);
            CHECK((*dir_symlinks)->contains("bar"));
        }

        SECTION("consuming dangling symlinks") {
            auto dangling_symlinks_tree = artifacts.value()->at("baz");
            auto consume_action =
                api->CreateAction(dangling_symlinks_tree.digest,
                                  {"/bin/sh",
                                   "-c",
                                   "set -e; "
                                   "[ \"$(readlink foo)\" = \"none\" ]; "
                                   "[ \"$(readlink bar)\" = \"none\" ]; "
                                   "touch success"},
                                  "",
                                  {"success"},
                                  {},
                                  {},
                                  props);

            auto const consume_response = consume_action->Execute();
            REQUIRE(consume_response);
            CHECK(consume_response->ExitCode() == 0);
        }
    }

    SECTION("upwards") {
        auto action = api->CreateAction(
            *api->UploadTree({}),
            {"/bin/sh",
             "-c",
             "set -e; "
             "ln -s ../foo foo; "
             "rm -rf bar; ln -s /bar bar; "
             "mkdir -p baz; ln -s ../foo baz/foo; ln -s /bar baz/bar"},
            "",
            {"foo"},
            {"bar", "baz"},
            {},
            props);

        // run execution
        auto const response = action->Execute();
        REQUIRE(response);

        // verify result
        auto const artifacts = response->Artifacts();
        REQUIRE(artifacts.has_value());

        if (ProtocolTraits::IsNative(api->GetHashType())) {
            // in native, no symlink is collected, as none of them is valid
            CHECK(artifacts.value()->empty());
            return;
        }

        REQUIRE(artifacts.value()->contains("foo"));
        CHECK(IsSymlinkObject(artifacts.value()->at("foo").type));
        REQUIRE(artifacts.value()->contains("bar"));
        CHECK(IsSymlinkObject(artifacts.value()->at("bar").type));
        REQUIRE(artifacts.value()->contains("baz"));
        CHECK(IsTreeObject(artifacts.value()->at("baz").type));

        // check if bar was correctly detected as directory symlink
        auto* local_response = dynamic_cast<LocalResponse*>(response.get());
        if (local_response != nullptr) {
            auto dir_symlinks = local_response->DirectorySymlinks();
            REQUIRE(dir_symlinks);
            CHECK((*dir_symlinks)->contains("bar"));
        }

        SECTION("consuming upwards symlinks") {
            auto upwards_symlinks_tree = artifacts.value()->at("baz");
            auto consume_action =
                api->CreateAction(upwards_symlinks_tree.digest,
                                  {"/bin/sh",
                                   "-c",
                                   "set -e; "
                                   "[ \"$(readlink foo)\" = \"../foo\" ]; "
                                   "[ \"$(readlink bar)\" = \"/bar\" ]; "
                                   "touch success"},
                                  "",
                                  {"success"},
                                  {},
                                  {},
                                  props);

            auto const consume_response = consume_action->Execute();
            REQUIRE(consume_response);
            CHECK(consume_response->ExitCode() == 0);
        }
    }
}

#endif  // INCLUDED_SRC_TEST_BUILDTOOL_EXECUTION_API_COMMON_API_TEST_HPP
