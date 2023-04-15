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

#ifndef INCLUDED_SRC_UTILS_CPP_JSON_HPP
#define INCLUDED_SRC_UTILS_CPP_JSON_HPP

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/utils/cpp/gsl.hpp"

template <typename ValueT>
auto ExtractValueAs(
    nlohmann::json const& j,
    std::string const& key,
    std::function<void(std::string const& error)>&& logger =
        [](std::string const& /*unused*/) -> void {}) noexcept
    -> std::optional<ValueT> {
    try {
        auto it = j.find(key);
        if (it == j.end()) {
            logger("key " + key + " cannot be found in JSON object");
            return std::nullopt;
        }
        return it.value().template get<ValueT>();
    } catch (std::exception& e) {
        logger(e.what());
        return std::nullopt;
    }
}

namespace detail {

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] static inline auto IndentListsOnlyUntilDepth(
    nlohmann::json const& json,
    std::string const& indent,
    std::size_t until,
    std::size_t depth) -> std::string {
    using iterator = std::ostream_iterator<std::string>;
    if (json.is_object()) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '{' << std::endl;
        for (auto const& [key, value] : json.items()) {
            std::fill_n(iterator{oss}, depth + 1, indent);
            oss << nlohmann::json(key).dump() << ": "
                << IndentListsOnlyUntilDepth(value, indent, until, depth + 1)
                << (++i == json.size() ? "" : ",") << std::endl;
        }
        std::fill_n(iterator{oss}, depth, indent);
        oss << '}';
        EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
        return oss.str();
    }
    if (json.is_array() and depth < until) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '[' << std::endl;
        for (auto const& value : json) {
            std::fill_n(iterator{oss}, depth + 1, indent);
            oss << IndentListsOnlyUntilDepth(value, indent, until, depth + 1)
                << (++i == json.size() ? "" : ",") << std::endl;
        }
        std::fill_n(iterator{oss}, depth, indent);
        oss << ']';
        EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
        return oss.str();
    }
    return json.dump();
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] static inline auto IndentOnlyUntilDepth(
    nlohmann::json const& json,
    std::string const& indent,
    std::size_t until,
    std::size_t depth,
    std::optional<std::string> path,
    std::unordered_map<std::string, std::size_t> const& depths) -> std::string {
    using iterator = std::ostream_iterator<std::string>;
    if (path and depths.find(*path) != depths.end()) {
        until = depths.find(*path)->second;
    }
    if (json.is_object() and depth < until) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '{' << std::endl;
        for (auto const& [key, value] : json.items()) {
            std::fill_n(iterator{oss}, depth + 1, indent);
            oss << nlohmann::json(key).dump() << ": "
                << IndentOnlyUntilDepth(
                       value,
                       indent,
                       until,
                       depth + 1,
                       path ? std::optional<std::string>(*path + "/" + key)
                            : std::nullopt,
                       depths)
                << (++i == json.size() ? "" : ",") << std::endl;
        }
        std::fill_n(iterator{oss}, depth, indent);
        oss << '}';
        EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
        return oss.str();
    }
    if (json.is_array() and depth < until) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '[' << std::endl;
        for (auto const& value : json) {
            std::fill_n(iterator{oss}, depth + 1, indent);
            oss << IndentOnlyUntilDepth(
                       value, indent, until, depth + 1, std::nullopt, depths)
                << (++i == json.size() ? "" : ",") << std::endl;
        }
        std::fill_n(iterator{oss}, depth, indent);
        oss << ']';
        EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
        return oss.str();
    }
    return json.dump();
}

}  // namespace detail

/// \brief Dump json with indent. Indent lists only until specified depth.
[[nodiscard]] static inline auto IndentListsOnlyUntilDepth(
    nlohmann::json const& json,
    std::size_t indent,
    std::size_t until_depth = 0) -> std::string {
    return detail::IndentListsOnlyUntilDepth(
        json, std::string(indent, ' '), until_depth, 0);
}

// \brief Dump json with indent. Indent until the given list; for initial
// pure object-paths, alternative depths can be specified
[[nodiscard]] static inline auto IndentOnlyUntilDepth(
    nlohmann::json const& json,
    std::size_t indent,
    std::size_t until_depth,
    std::unordered_map<std::string, std::size_t> const& depths) -> std::string {
    return detail::IndentOnlyUntilDepth(
        json, std::string(indent, ' '), until_depth, 0, "", depths);
}

// \brief Dump json, replacing subexpressions at the given depths by "*".
// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] static inline auto TruncateJson(nlohmann::json const& json,
                                              std::size_t depth)
    -> std::string {
    if (depth == 0) {
        return "*";
    }
    if (json.is_object()) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '{';
        for (auto const& [key, value] : json.items()) {
            oss << nlohmann::json(key).dump() << ":"
                << TruncateJson(value, depth - 1)
                << (++i == json.size() ? "" : ",");
        }
        oss << '}';
        return oss.str();
    }
    if (json.is_array()) {
        std::size_t i{};
        std::ostringstream oss{};
        oss << '[';
        for (auto const& value : json) {
            oss << TruncateJson(value, depth - 1)
                << (++i == json.size() ? "" : ",");
        }
        oss << ']';
        return oss.str();
    }
    return json.dump();
}

[[nodiscard]] static inline auto AbbreviateJson(nlohmann::json const& json,
                                                std::size_t len)
    -> std::string {
    auto json_string = json.dump();
    if (json_string.size() <= len) {
        return json_string;
    }
    std::size_t i = 1;
    auto old_abbrev = TruncateJson(json, i);
    auto new_abbrev = old_abbrev;
    while (new_abbrev.size() <= len) {
        old_abbrev = new_abbrev;
        ++i;
        new_abbrev = TruncateJson(json, i);
    }
    return old_abbrev;
}

#endif  // INCLUDED_SRC_UTILS_CPP_JSON_HPP
