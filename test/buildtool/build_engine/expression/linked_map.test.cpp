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

#include <algorithm>

#include "catch2/catch.hpp"
#include "src/buildtool/build_engine/expression/linked_map.hpp"
#include "test/utils/container_matchers.hpp"

TEST_CASE("Empty map", "[linked_map]") {
    using map_t = LinkedMap<std::string, int>;

    auto map = map_t::MakePtr(map_t::underlying_map_t{});
    REQUIRE(map);
    CHECK(map->empty());

    auto empty_map = map_t::underlying_map_t{};
    map = map_t::MakePtr(map, empty_map);
    REQUIRE(map);
    CHECK(map->empty());

    auto empty_linked_map = map_t::MakePtr(empty_map);
    map = map_t::MakePtr(map, empty_linked_map);
    REQUIRE(map);
    CHECK(map->empty());
}

TEST_CASE("Lookup and iteration", "[linked_map]") {
    using map_t = LinkedMap<std::string, int>;
    constexpr int kCount{100};
    constexpr int kQ{10};  // kQ == gcd(kCount, kQ) && 0 < kCount / kQ < 10

    auto map = map_t::MakePtr("0", 0);
    REQUIRE(map);
    CHECK(not map->empty());
    CHECK(map->size() == 1);

    for (int i{1}; i < kCount; ++i) {
        auto update = map_t::underlying_map_t{{std::to_string(i / kQ), i}};
        if (i % 2 == 0) {  // update via underlying map
            map = map_t::MakePtr(map, update);
        }
        else {  // update via linked map ptr
            map = map_t::MakePtr(map, map_t::MakePtr(update));
        }
        REQUIRE(map);
        CHECK(map->size() == static_cast<std::size_t>((i / kQ) + 1));
    }

    SECTION("contains and lookup") {
        for (int i{0}; i < kCount / kQ; ++i) {
            auto key = std::to_string(i);
            // kQ-many values per key: i -> i*kQ + [0;kQ-1], expect last
            auto expect = i * kQ + (kQ - 1);
            CHECK(map->contains(key));
            CHECK(map->at(key) == expect);
        }
    }

    SECTION("iteration via ranged-based loop") {
        auto i = kQ - 1;
        for (auto const& el : *map) {
            CHECK(el.first == std::to_string(i / kQ));
            CHECK(el.second == i);
            i += kQ;
        }
    }

    SECTION("iteration via algorithm") {
        auto i = kQ - 1;
        std::for_each(std::begin(*map), std::end(*map), [&](auto const& el) {
            CHECK(el.first == std::to_string(i / kQ));
            CHECK(el.second == i);
            i += kQ;
        });
    }
}

class CopyCounter {
  public:
    CopyCounter() : count_{std::make_shared<size_t>()} {}
    CopyCounter(CopyCounter const& other) {
        ++(*other.count_);
        count_ = other.count_;
    }
    CopyCounter(CopyCounter&&) = default;
    ~CopyCounter() = default;
    auto operator=(CopyCounter const& other) -> CopyCounter& {
        if (this != &other) {
            ++(*other.count_);
            count_ = other.count_;
        }
        return *this;
    }
    auto operator=(CopyCounter&&) -> CopyCounter& = default;
    [[nodiscard]] auto Count() const -> std::size_t { return *count_; }

  private:
    // all copies of this object share the same counter
    std::shared_ptr<size_t> count_{};
};

TEST_CASE("Zero copies", "[linked_map]") {
    using map_t = LinkedMap<std::string, CopyCounter>;
    constexpr int kCount{100};

    auto map = map_t::Ptr{};

    SECTION("Via initializer list") {
        for (int i{0}; i < kCount; ++i) {
            map = map_t::MakePtr(map, {{std::to_string(i), CopyCounter{}}});
            REQUIRE(map);
        }

        for (int i{0}; i < kCount; ++i) {
            auto key = std::to_string(i);
            REQUIRE(map->contains(key));
            // underlying map's initializer_list produces a single copy
            CHECK(map->at(key).Count() == 1);
        }
    }

    SECTION("Via pair") {
        for (int i{0}; i < kCount; ++i) {
            map = map_t::MakePtr(map, {std::to_string(i), CopyCounter{}});
            REQUIRE(map);
        }

        for (int i{0}; i < kCount; ++i) {
            auto key = std::to_string(i);
            REQUIRE(map->contains(key));
            CHECK(map->at(key).Count() == 0);
        }
    }

    SECTION("Via key and value arguments") {
        for (int i{0}; i < kCount; ++i) {
            map = map_t::MakePtr(map, std::to_string(i), CopyCounter{});
            REQUIRE(map);
        }

        for (int i{0}; i < kCount; ++i) {
            auto key = std::to_string(i);
            REQUIRE(map->contains(key));
            CHECK(map->at(key).Count() == 0);
        }
    }

    SECTION("Via underlying map and emplace") {
        for (int i{0}; i < kCount; ++i) {
            map_t::underlying_map_t update{};
            update.emplace(std::to_string(i), CopyCounter());
            map = map_t::MakePtr(map, std::move(update));
            REQUIRE(map);
        }

        for (int i{0}; i < kCount; ++i) {
            auto key = std::to_string(i);
            REQUIRE(map->contains(key));
            CHECK(map->at(key).Count() == 0);
        }
    }

    SECTION("Via linked map ptr") {
        for (int i{0}; i < kCount; ++i) {
            auto update = map_t::MakePtr(std::to_string(i), CopyCounter{});
            map = map_t::MakePtr(map, std::move(update));
            REQUIRE(map);
        }

        for (int i{0}; i < kCount; ++i) {
            auto key = std::to_string(i);
            REQUIRE(map->contains(key));
            CHECK(map->at(key).Count() == 0);
        }
    }
}

// Custom container that holds a LinkedMap.
class CustomContainer {
  public:
    class Ptr;
    using linked_map_t = LinkedMap<int, int, Ptr>;

    // Special smart pointer for container that can be used as internal NextPtr
    // for LinkedMap by implementing IsNotNull(), LinkedMap(), and Make().
    class Ptr : public std::shared_ptr<CustomContainer> {
      public:
        [[nodiscard]] auto IsNotNull() const noexcept -> bool {
            return static_cast<bool>(*this);
        }
        [[nodiscard]] auto Map() const& -> linked_map_t const& {
            return (*this)->Map();
        }
        [[nodiscard]] static auto Make(linked_map_t&& map) -> Ptr {
            return Ptr{std::make_shared<CustomContainer>(std::move(map))};
        }
    };

    explicit CustomContainer(linked_map_t&& map) noexcept
        : map_{std::move(map)} {}
    [[nodiscard]] auto Map() & noexcept -> linked_map_t& { return map_; }

  private:
    linked_map_t map_{};
};

TEST_CASE("Custom NextPtr", "[linked_map]") {
    using map_t = LinkedMap<int, int, CustomContainer::Ptr>;
    constexpr int kCount{100};
    constexpr int kQ{10};

    auto container = CustomContainer::Ptr::Make(map_t{0, 0});
    REQUIRE(container);
    CHECK(container->Map().size() == 1);

    for (int i{1}; i < kCount; ++i) {
        container = CustomContainer::Ptr::Make(map_t{container, {{i / kQ, i}}});
        REQUIRE(container);
        CHECK(container->Map().size() == static_cast<std::size_t>(i / kQ + 1));
    }

    for (int i{0}; i < kCount / kQ; ++i) {
        auto key = i;
        // kQ-many values per key: i -> i*kQ + [0;kQ-1], expect last
        auto expect = i * kQ + (kQ - 1);
        CHECK(container->Map().contains(key));
        CHECK(container->Map().at(key) == expect);
    }
}

TEST_CASE("Hash computation", "[linked_map]") {
    using map_t = LinkedMap<std::string, int>;

    auto map = map_t::MakePtr("foo", 4711);  // NOLINT
    REQUIRE(map);
    CHECK(not map->empty());

    auto map_hash = std::hash<LinkedMap<std::string, int>>{}(*map);
    CHECK_FALSE(map_hash == 0);

    auto ptr_hash = std::hash<LinkedMapPtr<std::string, int>>{}(map);
    CHECK_FALSE(ptr_hash == 0);
    CHECK(ptr_hash == map_hash);

    map = map_t::MakePtr(map, "foo", 4711);  // NOLINT
    auto dup_hash = std::hash<LinkedMapPtr<std::string, int>>{}(map);
    CHECK_FALSE(dup_hash == 0);
    CHECK(dup_hash == map_hash);

    map = map_t::MakePtr(map, "bar", 4712);  // NOLINT
    auto upd_hash = std::hash<LinkedMapPtr<std::string, int>>{}(map);
    CHECK_FALSE(upd_hash == 0);
    CHECK_FALSE(upd_hash == map_hash);
}
