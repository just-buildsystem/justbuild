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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_LINKED_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_LINKED_MAP_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <utility>  // std::move
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/multithreading/atomic_value.hpp"
#include "src/utils/cpp/atomic.hpp"
#include "src/utils/cpp/hash_combine.hpp"

template <class K, class V, class NextPtr>
class LinkedMap;

// Default NextPtr for LinkedMap, based on std::shared_ptr.
template <class K, class V>
class LinkedMapPtr {
    using ptr_t = LinkedMapPtr<K, V>;
    using map_t = LinkedMap<K, V, ptr_t>;

  public:
    LinkedMapPtr() noexcept = default;
    explicit LinkedMapPtr(std::shared_ptr<map_t> ptr) noexcept
        : ptr_{std::move(ptr)} {}
    explicit operator bool() const { return static_cast<bool>(ptr_); }
    [[nodiscard]] auto operator*() const& -> map_t const& { return *ptr_; }
    [[nodiscard]] auto operator->() const& -> map_t const* {
        return ptr_.get();
    }
    [[nodiscard]] auto IsNotNull() const noexcept -> bool {
        return static_cast<bool>(ptr_);
    }
    [[nodiscard]] auto Map() const& -> map_t const& { return *ptr_; }
    [[nodiscard]] static auto Make(map_t&& map) -> ptr_t {
        return ptr_t{std::make_shared<map_t>(std::move(map))};
    }

  private:
    std::shared_ptr<map_t> ptr_{};
};

/// \brief Immutable LinkedMap.
/// Uses smart pointers to build up a list of pointer-linked maps. The NextPtr
/// that is used internally can be overloaded by any class implementing the
/// following methods:
///     1. auto IsNotNull() const noexcept -> bool;
///     2. auto LinkedMap() const& -> LinkedMap<K, V, NextPtr> const&;
///     3. static auto Make(LinkedMap<K, V, NextPtr>&&) -> NextPtr;
template <class K, class V, class NextPtr = LinkedMapPtr<K, V>>
class LinkedMap {
    using item_t = std::pair<K, V>;
    using items_t = std::vector<item_t>;
    using keys_t = std::vector<K>;
    using values_t = std::vector<V>;

  public:
    using Ptr = NextPtr;
    // When merging maps, we always rely on entries being traversed in key
    // order; so keep the underlying map an ordered data structure.
    using underlying_map_t = std::map<K, V>;

    static constexpr auto MakePtr(underlying_map_t map) -> Ptr {
        return Ptr::Make(LinkedMap<K, V, Ptr>{std::move(map)});
    }

    static constexpr auto MakePtr(item_t item) -> Ptr {
        return Ptr::Make(LinkedMap<K, V, Ptr>{std::move(item)});
    }

    static constexpr auto MakePtr(K key, V value) -> Ptr {
        return Ptr::Make(
            LinkedMap<K, V, Ptr>{std::move(key), std::move(value)});
    }

    static constexpr auto MakePtr(Ptr next, Ptr content) -> Ptr {
        return Ptr::Make(LinkedMap<K, V, Ptr>{next, content});
    }

    static constexpr auto MakePtr(Ptr next, underlying_map_t map) -> Ptr {
        return Ptr::Make(LinkedMap<K, V, Ptr>{next, std::move(map)});
    }

    static constexpr auto MakePtr(Ptr const& next, item_t item) -> Ptr {
        return Ptr::Make(LinkedMap<K, V, Ptr>{next, std::move(item)});
    }

    static constexpr auto MakePtr(Ptr const& next, K key, V value) -> Ptr {
        return Ptr::Make(
            LinkedMap<K, V, Ptr>{next, std::move(key), std::move(value)});
    }

    explicit LinkedMap(underlying_map_t map) noexcept : map_{std::move(map)} {}
    explicit LinkedMap(item_t item) noexcept { map_.emplace(std::move(item)); }
    LinkedMap(K key, V val) noexcept {
        map_.emplace(std::move(key), std::move(val));
    }
    LinkedMap(Ptr next, Ptr content) noexcept
        : next_{std::move(next)}, content_{std::move(content)} {}
    LinkedMap(Ptr next, underlying_map_t map) noexcept
        : next_{std::move(next)}, map_{std::move(map)} {}
    LinkedMap(Ptr next, item_t item) noexcept : next_{std::move(next)} {
        map_.emplace(std::move(item));
    }
    LinkedMap(Ptr next, K key, V val) noexcept : next_{std::move(next)} {
        map_.emplace(std::move(key), std::move(val));
    }

    LinkedMap() noexcept = default;
    LinkedMap(LinkedMap const& other) noexcept = delete;
    LinkedMap(LinkedMap&& other) noexcept = default;
    ~LinkedMap() noexcept = default;

    auto operator=(LinkedMap const& other) noexcept = delete;
    auto operator=(LinkedMap&& other) noexcept = delete;

    [[nodiscard]] auto contains(K const& key) const noexcept -> bool {
        return static_cast<bool>(Find(key));
    }

    [[nodiscard]] auto at(K const& key) const& -> V const& {
        auto value = Find(key);
        if (value) {
            return **value;
        }
        throw std::out_of_range{fmt::format("Missing key {}", key)};
    }

    [[nodiscard]] auto at(K const& key) && -> V {
        auto value = Find(key);
        if (value) {
            return std::move(*value);
        }
        throw std::out_of_range{fmt::format("Missing key {}", key)};
    }

    [[nodiscard]] auto operator[](K const& key) const& -> V const& {
        return at(key);
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return (content_.IsNotNull() ? content_.Map().empty()
                                     : map_.empty()) and
               (not next_.IsNotNull() or next_.Map().empty());
    }

    [[nodiscard]] auto Find(K const& key) const& noexcept
        -> std::optional<V const*> {
        if (content_.IsNotNull()) {
            auto val = content_.Map().Find(key);
            if (val) {
                return val;
            }
        }
        else {
            auto it = map_.find(key);
            if (it != map_.end()) {
                return &it->second;
            }
        }
        if (next_.IsNotNull()) {
            auto val = next_.Map().Find(key);
            if (val) {
                return val;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto Find(K const& key) && noexcept -> std::optional<V> {
        if (content_.IsNotNull()) {
            auto val = content_.Map().Find(key);
            if (val) {
                return **val;
            }
        }
        else {
            auto it = map_.find(key);
            if (it != map_.end()) {
                return std::move(it->second);
            }
        }
        if (next_.IsNotNull()) {
            auto val = next_.Map().Find(key);
            if (val) {
                return **val;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto FindConflictingDuplicate(LinkedMap const& other)
        const& noexcept -> std::optional<std::reference_wrapper<K const>> {
        auto const& my_items = Items();
        auto const& other_items = other.Items();
        // Search for duplicates, using that iteration over the items is
        // ordered by keys.
        auto me = my_items.begin();
        auto they = other_items.begin();
        while (me != my_items.end() and they != other_items.end()) {
            if (me->first == they->first) {
                if (not(me->second == they->second)) {
                    return me->first;
                }
                ++me;
                ++they;
            }
            else if (me->first < they->first) {
                ++me;
            }
            else {
                ++they;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto FindConflictingDuplicate(
        LinkedMap const& other) && noexcept = delete;

    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return Items().size();
    }
    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto begin() const& -> typename items_t::const_iterator {
        return Items().cbegin();
    }
    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto end() const& -> typename items_t::const_iterator {
        return Items().cend();
    }
    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto cbegin() const& -> typename items_t::const_iterator {
        return begin();
    }
    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto cend() const& -> typename items_t::const_iterator {
        return end();
    }

    [[nodiscard]] auto begin() && -> typename items_t::const_iterator = delete;
    [[nodiscard]] auto end() && -> typename items_t::const_iterator = delete;
    [[nodiscard]] auto cbegin() && -> typename items_t::const_iterator = delete;
    [[nodiscard]] auto cend() && -> typename items_t::const_iterator = delete;

    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto operator==(
        LinkedMap<K, V, NextPtr> const& other) const noexcept -> bool {
        return this == &other or (this->empty() and other.empty()) or
               this->Items() == other.Items();
    }

    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto Items() const& -> items_t const& {
        return items_.SetOnceAndGet([this] { return ComputeSortedItems(); });
    }

    [[nodiscard]] auto Items() && = delete;

    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto Keys() const -> keys_t {
        auto keys = keys_t{};
        auto const& items = Items();
        keys.reserve(items.size());
        std::transform(items.begin(),
                       items.end(),
                       std::back_inserter(keys),
                       [](auto const& item) { return item.first; });
        return keys;
    }

    // NOTE: Expensive, needs to compute sorted items.
    [[nodiscard]] auto Values() const -> values_t {
        auto values = values_t{};
        auto const& items = Items();
        values.reserve(items.size());
        std::transform(items.begin(),
                       items.end(),
                       std::back_inserter(values),
                       [](auto const& item) { return item.second; });
        return values;
    }

  private:
    Ptr next_{};              // map that is shadowed by this map
    Ptr content_{};           // content of this map if set
    underlying_map_t map_{};  // content of this map if content_ is not set

    AtomicValue<items_t> items_{};

    [[nodiscard]] auto ComputeSortedItems() const noexcept -> items_t {
        auto size = content_.IsNotNull() ? content_.Map().size() : map_.size();
        if (next_.IsNotNull()) {
            size += next_.Map().size();
        }

        auto items = items_t{};
        items.reserve(size);

        auto empty = items_t{};
        auto map_copy = items_t{};
        typename items_t::const_iterator citemsit;
        typename items_t::const_iterator citemsend;
        typename items_t::const_iterator nitemsit;
        typename items_t::const_iterator nitemsend;

        if (content_.IsNotNull()) {
            auto const& citems = content_.Map().Items();
            citemsit = citems.begin();
            citemsend = citems.end();
        }
        else {
            map_copy.reserve(map_.size());
            map_copy.insert(map_copy.end(), map_.begin(), map_.end());
            citemsit = map_copy.begin();
            citemsend = map_copy.end();
        }
        if (next_.IsNotNull()) {
            auto const& nitems = next_.Map().Items();
            nitemsit = nitems.begin();
            nitemsend = nitems.end();
        }
        else {
            nitemsit = empty.begin();
            nitemsend = empty.end();
        }

        while (citemsit != citemsend and nitemsit != nitemsend) {
            if (citemsit->first == nitemsit->first) {
                items.push_back(*citemsit);
                ++citemsit;
                ++nitemsit;
            }
            else if (citemsit->first < nitemsit->first) {
                items.push_back(*citemsit);
                ++citemsit;
            }
            else {
                items.push_back(*nitemsit);
                ++nitemsit;
            }
        }

        // No more comparisons to be made; copy over the remaining
        // entries
        items.insert(items.end(), citemsit, citemsend);
        items.insert(items.end(), nitemsit, nitemsend);

        return items;
    }
};

namespace std {
template <class K, class V, class N>
struct hash<LinkedMap<K, V, N>> {
    [[nodiscard]] auto operator()(LinkedMap<K, V, N> const& m) const noexcept
        -> std::size_t {
        size_t seed{};
        for (auto const& e : m) {
            hash_combine(&seed, e.first);
            hash_combine(&seed, e.second);
        }
        return seed;
    }
};
template <class K, class V>
struct hash<LinkedMapPtr<K, V>> {
    [[nodiscard]] auto operator()(LinkedMapPtr<K, V> const& p) const noexcept
        -> std::size_t {
        return std::hash<std::remove_cvref_t<decltype(*p)>>{}(*p);
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_LINKED_MAP_HPP
