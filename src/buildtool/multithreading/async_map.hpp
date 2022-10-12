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

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_HPP

#include <memory>
#include <mutex>  // unique_lock
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <utility>  // std::make_pair to use std::unordered_map's emplace()
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/multithreading/async_map_node.hpp"
#include "src/buildtool/multithreading/task.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

// Wrapper around map data structure for KeyT->AsyncMapNode<ValueT> that only
// exposes the possibility to retrieve the node for a certain key, adding it in
// case of the key not yet being present. Thread-safe. Map look-ups happen under
// a shared lock, and only in the case that key needs to be added to the
// underlying map we uniquely lock. This is the default map class used inside
// AsyncMapConsumer
template <typename KeyT, typename ValueT>
class AsyncMap {
  public:
    using Node = AsyncMapNode<KeyT, ValueT>;
    // Nodes will be passed onto tasks. Nodes are owned by this map. Nodes are
    // alive as long as this map lives.
    using NodePtr = Node*;

    explicit AsyncMap(std::size_t jobs) : width_{ComputeWidth(jobs)} {}

    AsyncMap() = default;

    /// \brief Retrieve node for certain key. Key and new node are emplaced in
    /// the map in case that the key does not exist already.
    /// \returns shared pointer to the Node associated to given key
    [[nodiscard]] auto GetOrCreateNode(KeyT const& key) -> NodePtr {
        auto* node_or_null = GetNodeOrNullFromSharedMap(key);
        return node_or_null != nullptr ? node_or_null : AddKey(key);
    }

    [[nodiscard]] auto GetPendingKeys() const -> std::vector<KeyT> {
        std::vector<KeyT> keys{};
        size_t s = 0;
        for (auto& i : map_) {
            s += i.size();
        }

        keys.reserve(s);
        for (auto& i : map_) {
            for (auto const& [key, node] : i) {
                if (not node->IsReady()) {
                    keys.emplace_back(key);
                }
            }
        }
        return keys;
    }

    void Clear(gsl::not_null<TaskSystem*> const& ts) {
        for (std::size_t i = 0; i < width_; ++i) {
            ts->QueueTask([i, this]() { map_[i].clear(); });
        }
    }

  private:
    constexpr static std::size_t kScalingFactor = 2;
    std::size_t width_{ComputeWidth(0)};
    std::vector<std::shared_mutex> m_{width_};
    std::vector<std::unordered_map<KeyT, std::unique_ptr<Node>>> map_{width_};

    constexpr static auto ComputeWidth(std::size_t jobs) -> std::size_t {
        if (jobs <= 0) {
            // Non-positive indicates to use the default value
            return ComputeWidth(
                std::max(1U, std::thread::hardware_concurrency()));
        }
        return jobs * kScalingFactor + 1;
    }

    [[nodiscard]] auto GetNodeOrNullFromSharedMap(KeyT const& key) -> NodePtr {
        auto part = std::hash<KeyT>{}(key) % width_;
        std::shared_lock sl{m_[part]};
        auto it_to_key_pair = map_[part].find(key);
        if (it_to_key_pair != map_[part].end()) {
            // we know if the key is in the map then
            // the pair {key, node} is read only
            return it_to_key_pair->second.get();
        }
        return nullptr;
    }

    [[nodiscard]] auto AddKey(KeyT const& key) -> NodePtr {
        auto part = std::hash<KeyT>{}(key) % width_;
        std::unique_lock ul{m_[part]};
        auto it_to_key_pair = map_[part].find(key);
        if (it_to_key_pair != map_[part].end()) {
            return it_to_key_pair->second.get();
        }
        auto new_node = std::make_unique<Node>(key);
        bool unused{};
        std::tie(it_to_key_pair, unused) =
            map_[part].emplace(std::make_pair(key, std::move(new_node)));
        return it_to_key_pair->second.get();
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_HPP
