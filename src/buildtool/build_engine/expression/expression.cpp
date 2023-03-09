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

#include "src/buildtool/build_engine/expression/expression.hpp"

#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

#include "fmt/core.h"
#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/json.hpp"

auto Expression::operator[](
    std::string const& key) const& -> ExpressionPtr const& {
    auto value = Map().Find(key);
    if (value) {
        return value->get();
    }
    throw ExpressionTypeError{
        fmt::format("Map does not contain key '{}'.", key)};
}

auto Expression::operator[](std::string const& key) && -> ExpressionPtr {
    auto value = std::move(*this).Map().Find(key);
    if (value) {
        return std::move(*value);
    }
    throw ExpressionTypeError{
        fmt::format("Map does not contain key '{}'.", key)};
}

auto Expression::operator[](
    ExpressionPtr const& key) const& -> ExpressionPtr const& {
    return (*this)[key->String()];
}

auto Expression::operator[](ExpressionPtr const& key) && -> ExpressionPtr {
    return std::move(*this)[key->String()];
}

auto Expression::operator[](size_t pos) const& -> ExpressionPtr const& {
    if (pos < List().size()) {
        return List()[pos];
    }
    throw ExpressionTypeError{
        fmt::format("List pos '{}' is out of bounds.", pos)};
}

auto Expression::operator[](size_t pos) && -> ExpressionPtr {
    auto&& list = std::move(*this).List();
    if (pos < list.size()) {
        return list[pos];
    }
    throw ExpressionTypeError{
        fmt::format("List pos '{}' is out of bounds.", pos)};
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::ToJson(Expression::JsonMode mode) const -> nlohmann::json {
    if (IsBool()) {
        return Bool();
    }
    if (IsNumber()) {
        return Number();
    }
    if (IsString()) {
        return String();
    }
    if (IsArtifact() and mode != JsonMode::NullForNonJson) {
        return Artifact().ToJson();
    }
    if (IsResult() and mode != JsonMode::NullForNonJson) {
        auto const& result = Result();
        return Expression{map_t{{{"artifact_stage", result.artifact_stage},
                                 {"runfiles", result.runfiles},
                                 {"provides", result.provides}}}}
            .ToJson(JsonMode::SerializeAllButNodes);
    }
    if (IsNode() and mode != JsonMode::NullForNonJson) {
        switch (mode) {
            case JsonMode::SerializeAll:
                return Node().ToJson();
            case JsonMode::SerializeAllButNodes:
                return {{"type", "NODE"}, {"id", ToIdentifier()}};
            default:
                break;
        }
    }
    if (IsList()) {
        auto json = nlohmann::json::array();
        auto const& list = List();
        std::transform(list.begin(),
                       list.end(),
                       std::back_inserter(json),
                       // NOLINTNEXTLINE(misc-no-recursion)
                       [mode](auto const& e) { return e->ToJson(mode); });
        return json;
    }
    if (IsMap()) {
        auto json = nlohmann::json::object();
        auto const& map = Value<map_t>()->get();
        // NOLINTNEXTLINE(misc-no-recursion)
        std::for_each(map.begin(), map.end(), [&](auto const& p) {
            json.emplace(p.first, p.second->ToJson(mode));
        });
        return json;
    }
    if (IsName() and mode != JsonMode::NullForNonJson) {
        return Name().ToJson();
    }
    return nlohmann::json{};
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::ComputeIsCacheable() const -> bool {
    // Must be updated whenever we add a new non-cacheable value
    if (IsName()) {
        return false;
    }
    if (IsResult()) {
        return Result().is_cacheable;
    }
    if (IsNode()) {
        return Node().IsCacheable();
    }
    if (IsList()) {
        for (auto const& entry : List()) {
            if (not entry->IsCacheable()) {
                return false;
            }
        }
    }
    if (IsMap()) {
        for (auto const& [key, entry] : Map()) {
            if (not entry->IsCacheable()) {
                return false;
            }
        }
    }
    return true;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::ToString() const -> std::string {
    return ToJson().dump();
}

[[nodiscard]] auto Expression::ToAbbrevString(size_t len) const -> std::string {
    return AbbreviateJson(ToJson(), len);
}
// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::ToHash() const noexcept -> std::string {
    return hash_.SetOnceAndGet([this] { return ComputeHash(); });
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::IsCacheable() const -> bool {
    return is_cachable_.SetOnceAndGet([this] { return ComputeIsCacheable(); });
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::FromJson(nlohmann::json const& json) noexcept
    -> ExpressionPtr {
    if (json.is_null()) {
        return ExpressionPtr{none_t{}};
    }
    try {  // try-catch because json.get<>() could throw, although checked
        if (json.is_boolean()) {
            return ExpressionPtr{json.get<bool>()};
        }
        if (json.is_number()) {
            return ExpressionPtr{json.get<number_t>()};
        }
        if (json.is_string()) {
            return ExpressionPtr{std::string{json.get<std::string>()}};
        }
        if (json.is_array()) {
            auto l = Expression::list_t{};
            l.reserve(json.size());
            std::transform(json.begin(),
                           json.end(),
                           std::back_inserter(l),
                           // NOLINTNEXTLINE(misc-no-recursion)
                           [](auto const& j) { return FromJson(j); });
            return ExpressionPtr{l};
        }
        if (json.is_object()) {
            auto m = Expression::map_t::underlying_map_t{};
            for (auto const& el : json.items()) {
                m.emplace(el.key(), FromJson(el.value()));
            }
            return ExpressionPtr{Expression::map_t{m}};
        }
    } catch (...) {
        gsl_EnsuresAudit(false);  // ensure that the try-block never throws
    }
    return ExpressionPtr{nullptr};
}

template <size_t kIndex>
auto Expression::TypeStringForIndex() const noexcept -> std::string {
    using var_t = decltype(data_);
    if (kIndex == data_.index()) {
        return TypeToString<std::variant_alternative_t<kIndex, var_t>>();
    }
    constexpr auto size = std::variant_size_v<var_t>;
    if constexpr (kIndex < size - 1) {
        return TypeStringForIndex<kIndex + 1>();
    }
    return TypeToString<std::variant_alternative_t<size - 1, var_t>>();
}

auto Expression::TypeString() const noexcept -> std::string {
    return TypeStringForIndex();
}

// NOLINTNEXTLINE(misc-no-recursion)
auto Expression::ComputeHash() const noexcept -> std::string {
    auto hash = std::string{};
    if (IsNone() or IsBool() or IsNumber() or IsString() or IsArtifact() or
        IsResult() or IsNode() or IsName()) {
        // just hash the JSON representation, but prepend "@" for artifact,
        // "=" for result, "#" for node, and "$" for name.
        std::string prefix{IsArtifact() ? "@"
                           : IsResult() ? "="
                           : IsNode()   ? "#"
                           : IsName()   ? "$"
                                        : ""};
        hash = HashFunction::ComputeHash(prefix + ToString()).Bytes();
    }
    else {
        auto hasher = HashFunction::Hasher();
        if (IsList()) {
            auto list = Value<Expression::list_t>();
            hasher.Update("[");
            for (auto const& el : list->get()) {
                hasher.Update(el->ToHash());
            }
        }
        else if (IsMap()) {
            auto map = Value<Expression::map_t>();
            hasher.Update("{");
            for (auto const& el : map->get()) {
                hasher.Update(HashFunction::ComputeHash(el.first).Bytes());
                hasher.Update(el.second->ToHash());
            }
        }
        hash = std::move(hasher).Finalize().Bytes();
    }
    return hash;
}
