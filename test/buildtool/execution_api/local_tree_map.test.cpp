#include <string>
#include <thread>
#include <vector>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/common/local_tree_map.hpp"
#include "src/utils/cpp/atomic.hpp"

namespace {

[[nodiscard]] auto ToDigest(std::string const& s) {
    return static_cast<bazel_re::Digest>(
        ArtifactDigest{s, 0, /*is_tree=*/false});
}

[[nodiscard]] auto ToInfo(std::string const& s) {
    return Artifact::ObjectInfo{ArtifactDigest{s, 0, /*is_tree=*/false},
                                ObjectType::File};
}

}  // namespace

TEST_CASE("LocalTree: empty tree", "[execution_api]") {
    LocalTreeMap tree_map{};

    auto tree = tree_map.CreateTree();
    CHECK(tree.size() == 0);
    CHECK(std::all_of(
        tree.begin(), tree.end(), [](auto /*unused*/) { return false; }));
}

TEST_CASE("LocalTree: first wins", "[execution_api]") {
    LocalTreeMap tree_map{};

    auto tree = tree_map.CreateTree();
    CHECK(tree.AddInfo("foo", ToInfo("bar")));
    CHECK(tree.AddInfo("foo", ToInfo("baz")));
    CHECK(tree.size() == 1);
    for (auto const& [path, oid] : tree) {
        CHECK(oid->digest.hash() == "bar");
    }
}

TEST_CASE("LocalTreeMap: first wins", "[execution_api]") {
    LocalTreeMap tree_map{};

    auto tree_1 = tree_map.CreateTree();
    CHECK(tree_1.AddInfo("foo", ToInfo("bar")));

    auto tree_2 = tree_map.CreateTree();
    CHECK(tree_2.AddInfo("foo", ToInfo("baz")));

    auto tree_id = ToDigest("tree");
    CHECK(tree_map.AddTree(tree_id, std::move(tree_1)));  // NOLINT
    CHECK(tree_map.AddTree(tree_id, std::move(tree_2)));  // NOLINT

    CHECK(tree_map.HasTree(tree_id));

    auto const* tree = tree_map.GetTree(tree_id);
    REQUIRE(tree != nullptr);
    CHECK(tree->size() == 1);
    for (auto const& [path, oid] : *tree) {
        CHECK(oid->digest.hash() == "bar");
    }
}

TEST_CASE("LocalTreeMap: thread-safety", "[execution_api]") {
    constexpr auto kNumThreads = 100;
    constexpr auto kQ = 10;

    atomic<bool> starting_signal{false};
    std::vector<std::thread> threads{};
    threads.reserve(kNumThreads);

    LocalTreeMap tree_map{};

    for (int id{}; id < kNumThreads; ++id) {
        threads.emplace_back(
            [&tree_map, &starting_signal](int tid) {
                auto entry_id = std::to_string(tid);
                auto tree = tree_map.CreateTree();
                REQUIRE(tree.AddInfo(entry_id, ToInfo(entry_id)));

                auto tree_id = ToDigest(std::to_string(tid / kQ));
                starting_signal.wait(false);

                // kQ-many threads try to add tree with same id
                REQUIRE(tree_map.AddTree(tree_id, std::move(tree)));  // NOLINT
            },
            id);
    }

    starting_signal = true;
    starting_signal.notify_all();
    for (auto& thread : threads) {
        thread.join();
    }

    for (int id{}; id <= (kNumThreads - 1) / kQ; ++id) {
        auto tree_id = ToDigest(std::to_string(id));
        CHECK(tree_map.HasTree(tree_id));

        auto const* tree = tree_map.GetTree(tree_id);
        REQUIRE(tree != nullptr);
        CHECK(tree->size() == 1);
        for (auto const& [path, oid] : *tree) {
            auto entry_id = std::stoi(oid->digest.hash());
            CHECK(entry_id >= id * kQ);
            CHECK(entry_id < (id + 1) * kQ);
        }
    }
}
