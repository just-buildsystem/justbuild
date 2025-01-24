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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_CONFIGURATION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_CONFIGURATION_HPP

#include <algorithm>
#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>  // std::move
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/utils/cpp/concepts.hpp"
#include "src/utils/cpp/gsl.hpp"

// Decorator for Expression containing a map. Adds Prune() and Update().
class Configuration {
  public:
    explicit Configuration(ExpressionPtr expr) noexcept
        : expr_{std::move(expr)} {
        ExpectsAudit(expr_->IsMap());
    }
    explicit Configuration(Expression::map_t&& map) noexcept
        : expr_{ExpressionPtr{std::move(map)}} {}

    Configuration() noexcept = default;
    ~Configuration() noexcept = default;
    Configuration(Configuration const&) noexcept = default;
    Configuration(Configuration&&) noexcept = default;
    auto operator=(Configuration const&) noexcept -> Configuration& = default;
    auto operator=(Configuration&&) noexcept -> Configuration& = default;

    [[nodiscard]] auto operator[](std::string const& key) const
        -> ExpressionPtr {
        return expr_->Get(key, Expression::none_t{});
    }
    [[nodiscard]] auto operator[](ExpressionPtr const& key) const
        -> ExpressionPtr {
        return expr_->Get(key->String(), Expression::none_t{});
    }
    [[nodiscard]] auto ToString() const -> std::string {
        return expr_->ToString();
    }
    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        return expr_->ToJson();
    }
    [[nodiscard]] auto Enumerate(const std::string& prefix,
                                 std::size_t width) const -> std::string {
        std::stringstream ss{};
        if (width > prefix.size()) {
            std::size_t actual_width = width - prefix.size();
            for (auto const& [key, value] : expr_->Map()) {
                std::string key_str = Expression{key}.ToString();
                if (actual_width > key_str.size() + 3) {
                    ss << prefix << key_str << " : ";
                    std::size_t remain = actual_width - key_str.size() - 3;
                    std::string val_str = value->ToAbbrevString(remain);
                    if (val_str.size() >= remain) {
                        ss << val_str.substr(0, remain - 3) << "...";
                    }
                    else {
                        ss << val_str;
                    }
                }
                else {
                    ss << prefix << key_str.substr(0, actual_width);
                }
                ss << std::endl;
            }
        }
        return ss.str();
    }

    [[nodiscard]] auto operator==(const Configuration& other) const -> bool {
        return expr_ == other.expr_;
    }

    [[nodiscard]] auto operator<(const Configuration& other) const -> bool {
        return expr_->ToHash() < other.expr_->ToHash();
    }

    [[nodiscard]] auto hash() const noexcept -> std::size_t {
        return std::hash<ExpressionPtr>{}(expr_);
    }

    template <InputIterableStringContainer T>
    [[nodiscard]] auto Prune(T const& vars) const -> Configuration {
        auto subset = Expression::map_t::underlying_map_t{};
        std::for_each(vars.begin(), vars.end(), [&](auto const& k) {
            auto const& map = expr_->Map();
            auto v = map.Find(k);
            if (v) {
                subset.emplace(k, **v);
            }
            else {
                subset.emplace(k, Expression::kNone);
            }
        });
        return Configuration{Expression::map_t{subset}};
    }

    [[nodiscard]] auto Prune(ExpressionPtr const& vars) const -> Configuration {
        auto subset = Expression::map_t::underlying_map_t{};
        auto const& list = vars->List();
        std::for_each(list.begin(), list.end(), [&](auto const& k) {
            auto const& map = expr_->Map();
            auto const key = k->String();
            auto v = map.Find(key);
            if (v) {
                subset.emplace(key, **v);
            }
            else {
                subset.emplace(key, ExpressionPtr{Expression::none_t{}});
            }
        });
        return Configuration{Expression::map_t{subset}};
    }

    template <class T>
        requires(Expression::IsValidType<T>() or
                 std::is_same_v<T, ExpressionPtr>)
    [[nodiscard]] auto Update(std::string const& name,
                              T const& value) const -> Configuration {
        auto update = Expression::map_t::underlying_map_t{};
        update.emplace(name, value);
        return Configuration{Expression::map_t{expr_, update}};
    }

    [[nodiscard]] auto Update(
        Expression::map_t::underlying_map_t const& map) const -> Configuration {
        if (map.empty()) {
            return *this;
        }
        return Configuration{Expression::map_t{expr_, map}};
    }

    [[nodiscard]] auto Update(ExpressionPtr const& map) const -> Configuration {
        ExpectsAudit(map->IsMap());
        if (map->Map().empty()) {
            return *this;
        }
        return Configuration{Expression::map_t{expr_, map}};
    }

    [[nodiscard]] auto VariableFixed(std::string const& x) const -> bool {
        return expr_->Map().Find(x).has_value();
    }

    [[nodiscard]] auto Expr() const& -> ExpressionPtr const& { return expr_; }

    [[nodiscard]] auto Expr() && -> ExpressionPtr { return std::move(expr_); }

  private:
    ExpressionPtr expr_{Expression::kEmptyMap};
};

namespace std {
template <>
struct hash<Configuration> {
    [[nodiscard]] auto operator()(Configuration const& p) const noexcept
        -> std::size_t {
        return p.hash();
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_CONFIGURATION_HPP
