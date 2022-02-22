#ifndef INCLUDED_SRC_UTILS_CPP_JSON_HPP
#define INCLUDED_SRC_UTILS_CPP_JSON_HPP

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>

#include "nlohmann/json.hpp"
#include "gsl-lite/gsl-lite.hpp"

template <typename ValueT>
auto ExtractValueAs(
    nlohmann::json const& j,
    std::string const& key,
    std::function<void(std::string const& error)>&& logger =
        [](std::string const & /*unused*/) -> void {}) noexcept
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
        gsl_EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
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
        gsl_EnsuresAudit(nlohmann::json::parse(oss.str()) == json);
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

#endif  // INCLUDED_SRC_UTILS_CPP_JSON_HPP
