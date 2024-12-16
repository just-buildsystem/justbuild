// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_PRECOMPUTED_ROOT_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_PRECOMPUTED_ROOT_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "nlohmann/json.hpp"
#include "src/utils/cpp/expected.hpp"

struct ComputedRoot final {
    static constexpr auto kMarker = "computed";
    static constexpr std::size_t kSchemeLength = 5;

    std::string repository;
    std::string target_module;
    std::string target_name;
    nlohmann::json config;

    [[nodiscard]] auto operator==(ComputedRoot const& other) const noexcept
        -> bool;
    [[nodiscard]] auto operator<(ComputedRoot const& other) const noexcept
        -> bool;
    [[nodiscard]] auto ToString() const -> std::string;
    [[nodiscard]] auto ComputeHash() const -> std::size_t;
};

namespace std {
template <typename>
struct hash;
}

/// \brief Generalized representation of roots that must be evaluated before the
/// real build starts.
class PrecomputedRoot final {
  public:
    using root_t = std::variant<ComputedRoot>;
    explicit PrecomputedRoot() : PrecomputedRoot(ComputedRoot{}) {}
    explicit PrecomputedRoot(root_t root)
        : root_{std::move(root)}, hash_{ComputeHash(root_)} {}

    [[nodiscard]] static auto Parse(nlohmann::json const& root) noexcept
        -> expected<PrecomputedRoot, std::string>;

    [[nodiscard]] static auto IsPrecomputedMarker(
        std::string const& marker) noexcept -> bool {
        return marker == ComputedRoot::kMarker;
    }

    [[nodiscard]] auto operator==(PrecomputedRoot const& other) const noexcept
        -> bool {
        return root_ == other.root_;
    }
    [[nodiscard]] auto operator<(PrecomputedRoot const& other) const noexcept
        -> bool {
        return root_ < other.root_;
    }

    [[nodiscard]] auto ToString() const noexcept -> std::string;
    [[nodiscard]] auto GetReferencedRepository() const noexcept -> std::string;

    [[nodiscard]] auto IsComputed() const noexcept -> bool {
        return std::holds_alternative<ComputedRoot>(root_);
    }
    [[nodiscard]] auto AsComputed() const -> std::optional<ComputedRoot> {
        if (auto const* computed = std::get_if<ComputedRoot>(&root_)) {
            return *computed;
        }
        return std::nullopt;
    }

  private:
    root_t root_;
    std::size_t hash_;

    [[nodiscard]] static auto ComputeHash(root_t const& root) -> std::size_t;

    friend struct std::hash<PrecomputedRoot>;
};

namespace std {
template <>
struct hash<PrecomputedRoot> {
    [[nodiscard]] auto operator()(PrecomputedRoot const& root) const
        -> std::size_t {
        return root.hash_;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_PRECOMPUTED_ROOT_HPP
