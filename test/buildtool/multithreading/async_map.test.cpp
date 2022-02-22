#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/multithreading/async_map.hpp"
#include "src/buildtool/multithreading/async_map_node.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

TEST_CASE("Single-threaded: nodes only created once", "[async_map]") {
    AsyncMap<std::string, int> map;
    auto* key_node = map.GetOrCreateNode("key");
    CHECK(key_node != nullptr);

    auto* other_node = map.GetOrCreateNode("otherkey");
    CHECK(other_node != nullptr);

    auto* should_be_key_node = map.GetOrCreateNode("key");
    CHECK(should_be_key_node != nullptr);

    CHECK(key_node != other_node);
    CHECK(key_node == should_be_key_node);
}

TEST_CASE("Nodes only created once and survive the map destruction",
          "[async_map]") {

    using NodePtr = typename AsyncMap<std::string, int>::NodePtr;
    NodePtr key_node{nullptr};
    NodePtr other_node{nullptr};
    NodePtr should_be_key_node{nullptr};
    {
        AsyncMap<std::string, int> map;
        {
            TaskSystem ts;
            ts.QueueTask([&key_node, &map]() {
                auto* node = map.GetOrCreateNode("key");
                CHECK(node != nullptr);
                key_node = node;
            });

            ts.QueueTask([&other_node, &map]() {
                auto* node = map.GetOrCreateNode("otherkey");
                CHECK(node != nullptr);
                other_node = node;
            });

            ts.QueueTask([&should_be_key_node, &map]() {
                auto* node = map.GetOrCreateNode("key");
                CHECK(node != nullptr);
                should_be_key_node = node;
            });
        }
    }
    CHECK(key_node != nullptr);
    CHECK(other_node != nullptr);
    CHECK(should_be_key_node != nullptr);
    CHECK(key_node != other_node);
    CHECK(key_node == should_be_key_node);
}
