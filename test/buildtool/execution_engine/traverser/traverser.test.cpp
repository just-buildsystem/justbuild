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

#include "src/buildtool/execution_engine/traverser/traverser.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/artifact_description.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "test/utils/container_matchers.hpp"
#include "test/utils/hermeticity/test_hash_function_type.hpp"

namespace {

auto const kNumJobs = std::max(1U, std::thread::hardware_concurrency());

[[nodiscard]] auto IsArtifactLocal(nlohmann::json const& j) -> bool {
    return j.at("type") == "LOCAL";
}
class TestBuildInfo {
  public:
    [[nodiscard]] auto CorrectlyBuilt() const noexcept
        -> std::unordered_set<ArtifactIdentifier> {
        return correctly_built_;
    }

    [[nodiscard]] auto IncorrectlyBuilt() const noexcept
        -> std::unordered_set<ArtifactIdentifier> {
        return incorrectly_built_;
    }

    [[nodiscard]] auto ArtifactsUploaded() const noexcept
        -> std::unordered_set<ArtifactIdentifier> {
        return artifacts_uploaded_;
    }

    [[nodiscard]] auto WasUploadRepeated() noexcept -> bool {
        return not uploaded_more_than_once_.empty();
    }

    [[nodiscard]] auto Name() const noexcept -> std::string { return name_; }

    void SetName(std::string const& name) noexcept {
        std::lock_guard lock{mutex_};
        name_ = name;
    }

    void SetName(std::string&& name) noexcept {
        std::lock_guard lock{mutex_};
        name_ = std::move(name);
    }

    [[nodiscard]] auto InsertCorrectlyBuilt(
        ArtifactIdentifier const& artifact_id) -> bool {
        std::lock_guard lock{mutex_};
        auto const [_, first_time_added] = correctly_built_.insert(artifact_id);
        return first_time_added;
    }

    [[nodiscard]] auto InsertIncorrectlyBuilt(
        ArtifactIdentifier const& artifact_id) -> bool {
        std::lock_guard lock{mutex_};
        auto const [_, first_time_added] =
            incorrectly_built_.insert(artifact_id);
        return first_time_added;
    }

    auto InsertArtifactUploaded(ArtifactIdentifier const& artifact_id) -> bool {
        std::lock_guard lock{mutex_};
        auto const [_, first_time_added] =
            artifacts_uploaded_.insert(artifact_id);
        if (not first_time_added) {
            uploaded_more_than_once_.insert(artifact_id);
        }
        return true;
    }

  private:
    std::unordered_set<ArtifactIdentifier> correctly_built_;
    std::unordered_set<ArtifactIdentifier> incorrectly_built_;
    std::unordered_set<ArtifactIdentifier> artifacts_uploaded_;
    std::unordered_set<ArtifactIdentifier> uploaded_more_than_once_;
    std::string name_;
    std::mutex mutex_;
};

class TestExecutor {
  public:
    explicit TestExecutor(TestBuildInfo* info) noexcept
        : name_{info->Name()}, build_info_{info} {}

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ActionNode const*> const& action)
        const noexcept -> bool {
        try {
            build_info_->SetName(name_);
            bool const all_deps_available = AllAvailable(action->Children());
            if (all_deps_available) {
                return std::all_of(
                    action->OutputFiles().begin(),
                    action->OutputFiles().end(),
                    [this](auto const& entry) {
                        auto const& [name, node] = entry;
                        if (not build_info_->InsertCorrectlyBuilt(
                                node->Content().Id())) {
                            [[maybe_unused]] auto was_it_added =
                                build_info_->InsertIncorrectlyBuilt(
                                    node->Content().Id());
                            return false;
                        }
                        return true;
                    });
            }
            for (auto const& [name, node] : action->OutputFiles()) {
                [[maybe_unused]] auto was_it_added =
                    build_info_->InsertIncorrectlyBuilt(node->Content().Id());
            }
            return false;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] auto Process(
        gsl::not_null<DependencyGraph::ArtifactNode const*> const& artifact)
        const noexcept -> bool {
        try {
            build_info_->InsertArtifactUploaded(artifact->Content().Id());
        } catch (...) {
            return false;
        }
        return true;
    }

  private:
    std::string const name_;
    TestBuildInfo* build_info_;

    template <typename Container>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    [[nodiscard]] auto AllAvailable(Container&& c) const noexcept -> bool {
        return std::all_of(std::begin(c), std::end(c), [](auto node) {
            return node->TraversalState()->IsAvailable();
        });
    }
};

// Class to simplify the writing of tests, checking that no outputs are repeated
// and keeping track of what needs to be built
class TestProject {
  public:
    auto AddOutputInputPair(std::string const& action_id,
                            std::vector<std::string> const& outputs,
                            std::vector<nlohmann::json> const& inputs) -> bool {
        std::vector<std::string> command;
        command.emplace_back("BUILD");
        for (auto const& output : outputs) {
            command.push_back(output);
            auto const out_id = ArtifactDescription::CreateAction(
                                    action_id, std::filesystem::path{output})
                                    .Id();
            auto [_, is_inserted] = artifacts_to_be_built_.insert(out_id);
            if (not is_inserted) {
                return false;
            }
        }
        auto inputs_desc = ActionDescription::inputs_t{};
        if (not inputs.empty()) {
            command.emplace_back("FROM");
            auto const hash_type = TestHashType::ReadFromEnvironment();
            for (auto const& input_desc : inputs) {
                auto artifact =
                    ArtifactDescription::FromJson(hash_type, input_desc);
                REQUIRE(artifact);
                auto const input_id = artifact->Id();
                command.push_back(input_id);
                inputs_desc.emplace(input_id, *artifact);
                if (IsArtifactLocal(input_desc)) {
                    local_artifacts_.insert(input_id);
                }
            }
        }
        graph_full_description_.emplace_back(ActionDescription{
            outputs, {}, Action{action_id, command, {}}, inputs_desc});
        return true;
    }

    auto FillGraph(gsl::not_null<DependencyGraph*> const& g) -> bool {
        return g->Add(graph_full_description_);
    }

    [[nodiscard]] auto ArtifactsToBeBuilt() const noexcept
        -> std::unordered_set<ArtifactIdentifier> {
        return artifacts_to_be_built_;
    }

    [[nodiscard]] auto LocalArtifacts() const noexcept
        -> std::unordered_set<ArtifactIdentifier> {
        return local_artifacts_;
    }

  private:
    std::vector<ActionDescription> graph_full_description_;
    std::unordered_set<ArtifactIdentifier> artifacts_to_be_built_;
    std::unordered_set<ArtifactIdentifier> local_artifacts_;
};

}  // namespace

TEST_CASE("Executable", "[traverser]") {
    TestProject p;
    CHECK(p.AddOutputInputPair(
        "action",
        {"executable"},
        {ArtifactDescription::CreateLocal("main.cpp", "").ToJson()}));
    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Traverse()") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(executable)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);

            auto const exec_id =
                ArtifactDescription::CreateAction("action", "executable").Id();
            auto const traversed = traverser.Traverse({exec_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("Executable depends on library", "[traverser]") {
    TestProject p;
    CHECK(p.AddOutputInputPair(
        "make_exe",
        {"executable"},
        {ArtifactDescription::CreateLocal("main.cpp", "repo").ToJson(),
         ArtifactDescription::CreateAction("make_lib", "library").ToJson()}));
    CHECK(p.AddOutputInputPair(
        "make_lib",
        {"library"},
        {ArtifactDescription::CreateLocal("library.hpp", "repo").ToJson(),
         ArtifactDescription::CreateLocal("library.cpp", "repo").ToJson()}));
    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Full build (without specifying artifacts)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Full build (executable)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const exec_id =
                ArtifactDescription::CreateAction("make_exe", "executable")
                    .Id();
            CHECK(traverser.Traverse({exec_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Only build library") {
        auto const lib_id =
            ArtifactDescription::CreateAction("make_lib", "library").Id();
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({lib_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {lib_id}));
        CHECK(build_info.IncorrectlyBuilt().empty());
        auto const lib_cpp_id =
            ArtifactDescription::CreateLocal("library.cpp", "repo").Id();
        auto const lib_hpp_id =
            ArtifactDescription::CreateLocal("library.hpp", "repo").Id();
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {lib_cpp_id, lib_hpp_id}));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("Two artifacts depend on another", "[traverser]") {
    TestProject p;
    auto const description =
        ArtifactDescription::CreateAction("make_dep", "dep");
    auto const dep_desc = description.ToJson();
    auto const& dep_id = description.Id();
    CHECK(p.AddOutputInputPair("action1", {"toplevel1"}, {dep_desc}));
    CHECK(p.AddOutputInputPair("action2", {"toplevel2"}, {dep_desc}));
    CHECK(p.AddOutputInputPair(
        "make_dep",
        {"dep"},
        {ArtifactDescription::CreateLocal("leaf1", "repo").ToJson(),
         ArtifactDescription::CreateLocal("leaf2", "repo").ToJson()}));
    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Full build") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Only specified top-level artifact is built") {
        auto const toplevel1_id =
            ArtifactDescription::CreateAction("action1", "toplevel1").Id();
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({toplevel1_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {toplevel1_id, dep_id}));
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("Action with two outputs, no deps", "[traverser]") {
    TestProject p;
    CHECK(p.AddOutputInputPair("make_outputs", {"output1", "output2"}, {}));
    auto const output1_id =
        ArtifactDescription::CreateAction("make_outputs", "output1").Id();
    auto const output2_id =
        ArtifactDescription::CreateAction("make_outputs", "output2").Id();
    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Traverse()") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(output1)") {

        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({output1_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(output1, output2)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({output1_id, output2_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("Action with two outputs, one dep", "[traverser]") {
    TestProject p;
    CHECK(p.AddOutputInputPair(
        "make_outputs",
        {"output1", "output2"},
        {ArtifactDescription::CreateLocal("dep", "repo").ToJson()}));
    auto const output1_id =
        ArtifactDescription::CreateAction("make_outputs", "output1").Id();
    auto const output2_id =
        ArtifactDescription::CreateAction("make_outputs", "output2").Id();
    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Traverse()") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(output1)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({output1_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(output1, output2)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({output1_id, output2_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(dep, output2)") {
        auto const dep_id =
            ArtifactDescription::CreateLocal("dep", "repo").Id();
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({dep_id, output2_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("Action with two outputs, actions depend on each of outputs",
          "[traverser]") {
    TestProject p;
    CHECK(p.AddOutputInputPair("make_outputs", {"output1", "output2"}, {}));
    auto const desc_1 =
        ArtifactDescription::CreateAction("make_outputs", "output1");
    auto const output1_desc = desc_1.ToJson();
    auto const& output1_id = desc_1.Id();

    auto const desc_2 =
        ArtifactDescription::CreateAction("make_outputs", "output2");
    auto const output2_desc = desc_2.ToJson();
    auto const& output2_id = desc_2.Id();

    CHECK(p.AddOutputInputPair("consumer1", {"exec1"}, {output1_desc}));
    auto const exec1_id =
        ArtifactDescription::CreateAction("consumer1", "exec1").Id();

    CHECK(p.AddOutputInputPair("consumer2", {"exec2"}, {output2_desc}));
    auto const exec2_id =
        ArtifactDescription::CreateAction("consumer2", "exec2").Id();

    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted";
    build_info.SetName(name);
    SECTION("Traverse()") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(exec1)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({exec1_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {exec1_id, output1_id, output2_id}));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(exec2, output1)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({output1_id, exec2_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {exec2_id, output1_id, output2_id}));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Traverse(exec1, exec2)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            auto const traversed = traverser.Traverse({exec1_id, exec2_id});
            CHECK(traversed);
        }
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}

TEST_CASE("lib2 depends on lib1, executable depends on lib1 and lib2") {
    TestProject p;
    auto const desc_1 = ArtifactDescription::CreateAction("make_lib1", "lib1");
    auto const lib1_desc = desc_1.ToJson();
    auto const& lib1_id = desc_1.Id();

    auto const desc_2 = ArtifactDescription::CreateAction("make_lib2", "lib2");
    auto const lib2_desc = desc_2.ToJson();
    auto const& lib2_id = desc_2.Id();

    auto const exec_id =
        ArtifactDescription::CreateAction("make_exe", "executable").Id();

    CHECK(p.AddOutputInputPair(
        "make_exe",
        {"executable"},
        {ArtifactDescription::CreateLocal("main.cpp", "repo").ToJson(),
         lib1_desc,
         lib2_desc}));

    CHECK(p.AddOutputInputPair(
        "make_lib1",
        {"lib1"},
        {ArtifactDescription::CreateLocal("lib1.hpp", "repo").ToJson(),
         ArtifactDescription::CreateLocal("lib1.cpp", "repo").ToJson()}));
    CHECK(p.AddOutputInputPair(
        "make_lib2",
        {"lib2"},
        {lib1_desc,
         ArtifactDescription::CreateLocal("lib2.hpp", "repo").ToJson(),
         ArtifactDescription::CreateLocal("lib2.cpp", "repo").ToJson()}));

    DependencyGraph g;
    CHECK(p.FillGraph(&g));
    TestBuildInfo build_info;
    std::atomic<bool> failed{};
    std::string name = "This is a long name that shouldn't be corrupted ";
    build_info.SetName(name);
    SECTION(" Full build(without specifying artifacts) ") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse());
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Full build (executable)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({exec_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Full build (executable + lib1)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({exec_id, lib1_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Full build (executable + lib2)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({exec_id, lib2_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("Full build (executable + lib1 + lib2)") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({exec_id, lib1_id, lib2_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
    SECTION("First call does not build all artifacts") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({lib1_id}));
            CHECK(traverser.Traverse({exec_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.ArtifactsToBeBuilt()));
        CHECK(build_info.IncorrectlyBuilt().empty());
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                p.LocalArtifacts()));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }

    SECTION(
        "Traverse(lib2), executable is not built even if lib1 would notify its "
        "action") {
        {
            TestExecutor runner{&build_info};
            Traverser traverser(runner, g, kNumJobs, &failed);
            CHECK(traverser.Traverse({lib2_id}));
        }
        CHECK_FALSE(failed);
        CHECK_THAT(
            build_info.CorrectlyBuilt(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {lib1_id, lib2_id}));
        CHECK(build_info.IncorrectlyBuilt().empty());
        auto const lib1_hpp_id =
            ArtifactDescription::CreateLocal("lib1.hpp", "repo").Id();
        auto const lib1_cpp_id =
            ArtifactDescription::CreateLocal("lib1.cpp", "repo").Id();
        auto const lib2_hpp_id =
            ArtifactDescription::CreateLocal("lib2.hpp", "repo").Id();
        auto const lib2_cpp_id =
            ArtifactDescription::CreateLocal("lib2.cpp", "repo").Id();
        CHECK_THAT(
            build_info.ArtifactsUploaded(),
            HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                {lib1_hpp_id, lib1_cpp_id, lib2_hpp_id, lib2_cpp_id}));
        CHECK_FALSE(build_info.WasUploadRepeated());
        CHECK(build_info.Name() == name);
    }
}
