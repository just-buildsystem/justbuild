#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "test/utils/container_matchers.hpp"

/// \brief Checks that each artifact with identifier in output_ids has been
/// added to the graph and that its builder action has id action_id, and that
/// all outputs of actions are those the ids of which are listed in output_ids
void CheckOutputNodesCorrectlyAdded(
    DependencyGraph const& g,
    ActionIdentifier const& action_id,
    std::vector<std::string> const& output_paths) {
    std::vector<ArtifactIdentifier> output_ids;
    for (auto const& path : output_paths) {
        auto const output_id = ArtifactFactory::Identifier(
            ArtifactFactory::DescribeActionArtifact(action_id, path));
        CHECK(g.ArtifactWithId(output_id));
        auto const* action = g.ActionNodeOfArtifactWithId(output_id);
        CHECK(action != nullptr);
        CHECK(action->Content().Id() == action_id);
        output_ids.push_back(output_id);
    }
    CHECK_THAT(
        g.ActionNodeWithId(action_id)->OutputFileIds(),
        HasSameUniqueElementsAs<std::vector<ArtifactIdentifier>>(output_ids));
}

/// \brief Checks that the artifacts with ids in inputs_ids are in the graph and
/// coincide with the action's dependencies
void CheckInputNodesCorrectlyAdded(
    DependencyGraph const& g,
    ActionIdentifier const& action_id,
    std::vector<ArtifactIdentifier> const& input_ids) noexcept {
    for (auto const& input_id : input_ids) {
        CHECK(g.ArtifactWithId(input_id));
    }
    CHECK_THAT(
        g.ActionNodeWithId(action_id)->DependencyIds(),
        HasSameUniqueElementsAs<std::vector<ArtifactIdentifier>>(input_ids));
}

/// \brief Checks that the artifacts have been added as local artifact and their
/// local path is correct
void CheckLocalArtifactsCorrectlyAdded(
    DependencyGraph const& g,
    std::vector<ArtifactIdentifier> const& ids,
    std::vector<std::string> const& paths) noexcept {
    REQUIRE(ids.size() == paths.size());
    for (std::size_t pos = 0; pos < ids.size(); ++pos) {
        auto const* artifact_node = g.ArtifactNodeWithId(ids[pos]);
        CHECK(artifact_node != nullptr);
        CHECK(not artifact_node->HasBuilderAction());
        CHECK(artifact_node->Content().FilePath() == paths[pos]);
    }
}

TEST_CASE("Empty Dependency Graph", "[dag]") {
    DependencyGraph g;
    CHECK(g.IsValid());
}

TEST_CASE("AddAction({single action, single output, no inputs})", "[dag]") {
    std::string const action_id = "action_id";
    auto const action_description = ActionDescription{
        {"out"}, {}, Action{action_id, {"touch", "out"}, {}}, {}};
    DependencyGraph g;
    CHECK(g.AddAction(action_description));
    CheckOutputNodesCorrectlyAdded(g, action_id, {"out"});
    CHECK(g.IsValid());
}

TEST_CASE("AddAction({single action, more outputs, no inputs})", "[dag]") {
    std::string const action_id = "action_id";
    std::vector<std::string> const output_files = {"out0", "out1", "out2"};
    auto const action_description = ActionDescription{
        output_files,
        {},
        Action{action_id, {"touch", "out0", "out1", "out2"}, {}},
        {}};
    DependencyGraph g;
    CHECK(g.AddAction(action_description));
    CheckOutputNodesCorrectlyAdded(g, action_id, output_files);
    CHECK(g.IsValid());
}

TEST_CASE("AddAction({single action, single output, source file})", "[dag]") {
    using path = std::filesystem::path;
    std::string const action_id = "action_id";
    auto const src_description = ArtifactDescription{path{"main.cpp"}, "repo"};
    auto const src_id = src_description.Id();
    DependencyGraph g;
    SECTION("Input file in the same path than it is locally") {
        auto const action_description =
            ActionDescription{{"executable"},
                              {},
                              Action{action_id, {"gcc", "main.cpp"}, {}},
                              {{"main.cpp", src_description}}};
        CHECK(g.AddAction(action_description));
    }
    SECTION("Input file in different path from the local one") {
        auto const action_description =
            ActionDescription{{"executable"},
                              {},
                              Action{action_id, {"gcc", "src/a.cpp"}, {}},
                              {{"src/a.cpp", src_description}}};
        CHECK(g.Add({action_description}));
    }

    CheckOutputNodesCorrectlyAdded(g, action_id, {"executable"});
    CheckInputNodesCorrectlyAdded(g, action_id, {src_id});

    // Now we check that the src file artifact was added with the correct path
    CheckLocalArtifactsCorrectlyAdded(g, {src_id}, {"main.cpp"});

    // All artifacts are the source file and the executable
    CHECK_THAT(g.ArtifactIdentifiers(),
               HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                   {src_id,
                    ArtifactFactory::Identifier(
                        ArtifactFactory::DescribeActionArtifact(
                            action_id, "executable"))}));
    CHECK(g.IsValid());
}

TEST_CASE("AddAction({single action, single output, no inputs, env_variables})",
          "[dag]") {
    std::string const action_id = "action_id";
    std::string const name = "World";
    DependencyGraph g;
    std::vector<std::string> const command{
        "/bin/sh", "-c", "set -e\necho 'Hello, ${NAME}' > greeting"};
    nlohmann::json const env_vars{{"NAME", name}};
    auto const action_description = ActionDescription{
        {"greeting"}, {}, Action{action_id, command, env_vars}, {}};

    CHECK(g.AddAction(action_description));

    CheckOutputNodesCorrectlyAdded(g, action_id, {"greeting"});
    CheckInputNodesCorrectlyAdded(g, action_id, {});

    auto const* const action_node = g.ActionNodeWithId(action_id);
    CHECK(action_node != nullptr);
    CHECK(action_node->Command() == command);
    CHECK(action_node->Env() == env_vars);

    // All artifacts are the output file
    CHECK_THAT(g.ArtifactIdentifiers(),
               HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                   {ArtifactFactory::Identifier(
                       ArtifactFactory::DescribeActionArtifact(action_id,
                                                               "greeting"))}));
    CHECK(g.IsValid());
}

TEST_CASE("Add executable and library", "[dag]") {
    // Note: we don't use local bindings for members of pair as output of
    // functions because it seems to be problematic with Catch2's macros inside
    // lambdas and we want to use lambdas here to avoid repetition
    using path = std::filesystem::path;
    std::string const make_exec_id = "make_exe";
    std::string const make_lib_id = "make_lib";
    std::vector<std::string> const make_exec_cmd = {"build", "exec"};
    std::vector<std::string> const make_lib_cmd = {"build", "lib.a"};
    auto const main_desc = ArtifactDescription{path{"main.cpp"}, ""};
    auto const main_id = main_desc.Id();
    auto const lib_hpp_desc = ArtifactDescription{path{"lib/lib.hpp"}, ""};
    auto const lib_hpp_id = lib_hpp_desc.Id();
    auto const lib_cpp_desc = ArtifactDescription{path{"lib/lib.cpp"}, ""};
    auto const lib_cpp_id = lib_cpp_desc.Id();
    auto const lib_a_desc = ArtifactDescription{make_lib_id, "lib.a"};
    auto const lib_a_id = lib_a_desc.Id();

    auto const make_exec_desc =
        ActionDescription{{"exec"},
                          {},
                          Action{make_exec_id, make_exec_cmd, {}},
                          {{"main.cpp", main_desc}, {"lib.a", lib_a_desc}}};
    auto const exec_out_id = ArtifactDescription{make_exec_id, "exec"}.Id();

    auto const make_lib_desc = ActionDescription{
        {"lib.a"},
        {},
        Action{make_lib_id, make_lib_cmd, {}},
        {{"lib.hpp", lib_hpp_desc}, {"lib.cpp", lib_cpp_desc}}};

    DependencyGraph g;
    auto check_exec = [&]() {
        CHECK(g.IsValid());
        CheckOutputNodesCorrectlyAdded(g, make_exec_id, {"exec"});
        CheckInputNodesCorrectlyAdded(g, make_exec_id, {main_id, lib_a_id});
        CheckLocalArtifactsCorrectlyAdded(g, {main_id}, {"main.cpp"});
        CHECK_THAT(g.ActionNodeOfArtifactWithId(exec_out_id)->Command(),
                   Catch::Matchers::Equals(make_exec_cmd));
    };

    auto check_lib = [&]() {
        CHECK(g.IsValid());
        CheckOutputNodesCorrectlyAdded(g, make_lib_id, {"lib.a"});
        CheckInputNodesCorrectlyAdded(g, make_lib_id, {lib_hpp_id, lib_cpp_id});
        CheckLocalArtifactsCorrectlyAdded(
            g, {lib_hpp_id, lib_cpp_id}, {"lib/lib.hpp", "lib/lib.cpp"});
        CHECK_THAT(g.ActionNodeOfArtifactWithId(lib_a_id)->Command(),
                   Catch::Matchers::Equals(make_lib_cmd));
    };

    SECTION("First exec, then lib") {
        CHECK(g.AddAction(make_exec_desc));
        check_exec();
        CHECK(g.AddAction(make_lib_desc));
        check_lib();
    }

    SECTION("First lib, then exec") {
        CHECK(g.AddAction(make_lib_desc));
        check_lib();
        CHECK(g.AddAction(make_exec_desc));
        check_exec();
    }

    SECTION("Add both with single call to `DependencyGraph::Add`") {
        CHECK(g.Add({make_exec_desc, make_lib_desc}));
        check_exec();
        check_lib();
    }

    CHECK_THAT(g.ArtifactIdentifiers(),
               HasSameUniqueElementsAs<std::unordered_set<ArtifactIdentifier>>(
                   {main_id, exec_out_id, lib_a_id, lib_hpp_id, lib_cpp_id}));
}

// Incorrect action description tests

TEST_CASE("AddAction(id, empty action description) fails", "[dag]") {
    DependencyGraph g;
    CHECK(not g.AddAction(ActionDescription{{}, {}, Action{"id", {}, {}}, {}}));
}

TEST_CASE("AddAction(Empty mandatory non-empty field in action description)",
          "[dag]") {
    DependencyGraph g;
    CHECK(not g.AddAction(ActionDescription{
        {"output0", "output1"}, {}, Action{"empty command", {}, {}}, {}}));
    CHECK(not g.AddAction(ActionDescription{
        {}, {}, Action{"empty output", {"echo", "hello"}, {}}, {}}));
}

// Collision between actions tests

TEST_CASE("Adding cyclic dependencies produces invalid graph", "[dag]") {
    std::string const action1_id = "action1";
    std::string const action2_id = "action2";
    auto const out1_desc = ArtifactDescription(action1_id, "out1");
    auto const out1_id = out1_desc.Id();
    auto const out2_desc = ArtifactDescription(action2_id, "out2");
    auto const out2_id = out2_desc.Id();

    auto const action1_desc =
        ActionDescription{{"out1"},
                          {},
                          Action{action1_id, {"touch", "out1"}, {}},
                          {{"dep", out2_desc}}};
    auto const action2_desc =
        ActionDescription{{"out2"},
                          {},
                          Action{action2_id, {"touch", "out2"}, {}},
                          {{"dep", out1_desc}}};

    DependencyGraph g;
    CHECK(g.Add({action1_desc, action2_desc}));
    CHECK(not g.IsValid());
}

TEST_CASE("Error when adding an action with an id already added", "[dag]") {
    std::string const action_id = "id";
    auto const action_desc =
        ActionDescription{{"out"}, {}, Action{"id", {"touch", "out"}, {}}, {}};

    DependencyGraph g;
    CHECK(g.AddAction(action_desc));
    CheckOutputNodesCorrectlyAdded(g, action_id, {"out"});
    CHECK(g.IsValid());

    CHECK(not g.AddAction(action_desc));
}

TEST_CASE("Error when adding conflicting output files and directories",
          "[dag]") {
    auto const action_desc = ActionDescription{
        {"out"}, {"out"}, Action{"id", {"touch", "out"}, {}}, {}};

    DependencyGraph g;
    CHECK_FALSE(g.AddAction(action_desc));
}
