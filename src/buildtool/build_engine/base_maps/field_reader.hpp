#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

[[nodiscard]] static inline auto GetOrDefault(nlohmann::json const& json,
                                              std::string const& key,
                                              nlohmann::json&& default_value)
    -> nlohmann::json {
    auto value = json.find(key);
    if (value != json.end()) {
        return value.value();
    }
    return std::move(default_value);
}

class FieldReader {
  public:
    using Ptr = std::shared_ptr<FieldReader>;

    class EntityAliases {
      public:
        [[nodiscard]] auto Obtain() && -> std::pair<std::vector<std::string>,
                                                    std::vector<EntityName>> {
            return std::make_pair(std::move(names_), std::move(ids_));
        }
        auto reserve(std::size_t size) -> void {
            names_.reserve(size);
            ids_.reserve(size);
        }
        template <class T_Name, class T_Id>
        auto emplace_back(T_Name&& name, T_Id&& id) -> void {
            names_.emplace_back(std::forward<T_Name>(name));
            ids_.emplace_back(std::forward<T_Id>(id));
        }

      private:
        std::vector<std::string> names_;
        std::vector<EntityName> ids_;
    };

    [[nodiscard]] static auto Create(nlohmann::json const& json,
                                     EntityName const& id,
                                     std::string const& entity_type,
                                     AsyncMapConsumerLoggerPtr const& logger)
        -> std::optional<FieldReader> {
        if (not json.is_object()) {
            (*logger)(fmt::format("{} definition {} is not an object.",
                                  entity_type,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        return FieldReader(json, id, entity_type, logger);
    }

    [[nodiscard]] static auto CreatePtr(nlohmann::json const& json,
                                        EntityName const& id,
                                        std::string const& entity_type,
                                        AsyncMapConsumerLoggerPtr const& logger)
        -> Ptr {
        if (not json.is_object()) {
            (*logger)(fmt::format("{} definition {} is not an object.",
                                  entity_type,
                                  id.GetNamedTarget().name),
                      true);
            return nullptr;
        }
        return std::make_shared<FieldReader>(json, id, entity_type, logger);
    }

    [[nodiscard]] auto ReadExpression(std::string const& field_name) const
        -> ExpressionPtr {
        auto expr_it = json_.find(field_name);
        if (expr_it == json_.end()) {
            (*logger_)(fmt::format("Missing mandatory field {} in {} {}.",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return ExpressionPtr{nullptr};
        }

        auto expr = Expression::FromJson(expr_it.value());
        if (not expr) {
            (*logger_)(
                fmt::format("Failed to create expression from JSON:\n  {}",
                            json_.dump()),
                true);
        }
        return expr;
    }

    [[nodiscard]] auto ReadOptionalExpression(
        std::string const& field_name,
        ExpressionPtr const& default_value) const -> ExpressionPtr {
        auto expr_it = json_.find(field_name);
        if (expr_it == json_.end()) {
            return default_value;
        }

        auto expr = Expression::FromJson(expr_it.value());
        if (not expr) {
            (*logger_)(
                fmt::format("Failed to create expression from JSON:\n  {}",
                            json_.dump()),
                true);
        }
        return expr;
    }

    [[nodiscard]] auto ReadStringList(std::string const& field_name) const
        -> std::optional<std::vector<std::string>> {
        auto const& list =
            GetOrDefault(json_, field_name, nlohmann::json::array());
        if (not list.is_array()) {
            (*logger_)(fmt::format("Field {} in {} {} is not a list",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        auto vars = std::vector<std::string>{};
        vars.reserve(list.size());

        try {
            std::transform(
                list.begin(),
                list.end(),
                std::back_inserter(vars),
                [](auto const& j) { return j.template get<std::string>(); });
        } catch (...) {
            (*logger_)(fmt::format("List entry in {} of {} {} is not a string",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        return vars;
    }

    [[nodiscard]] auto ReadEntityAliasesObject(
        std::string const& field_name) const -> std::optional<EntityAliases> {
        auto const& map =
            GetOrDefault(json_, field_name, nlohmann::json::object());
        if (not map.is_object()) {
            (*logger_)(fmt::format("Field {} in {} {} is not an object",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        auto imports = EntityAliases{};
        imports.reserve(map.size());

        for (auto const& [key, val] : map.items()) {
            auto expr_id = ParseEntityNameFromJson(
                val,
                id_,
                [this, &field_name, entry = val.dump()](
                    std::string const& parse_err) {
                    (*logger_)(fmt::format("Parsing entry {} in field {} of {} "
                                           "{} failed with:\n{}",
                                           entry,
                                           field_name,
                                           entity_type_,
                                           id_.GetNamedTarget().name,
                                           parse_err),
                               true);
                });
            if (not expr_id) {
                return std::nullopt;
            }
            imports.emplace_back(key, *expr_id);
        }
        return imports;
    }

    void ExpectFields(std::unordered_set<std::string> const& expected) {
        auto unexpected = nlohmann::json::array();
        for (auto const& [key, value] : json_.items()) {
            if (not expected.contains(key)) {
                unexpected.push_back(key);
            }
        }
        if (not unexpected.empty()) {
            (*logger_)(fmt::format("{} {} has unexpected parameters {}",
                                   entity_type_,
                                   id_.ToString(),
                                   unexpected.dump()),
                       false);
        }
    }

    FieldReader(nlohmann::json json,
                EntityName id,
                std::string entity_type,
                AsyncMapConsumerLoggerPtr logger) noexcept
        : json_(std::move(json)),
          id_{std::move(id)},
          entity_type_{std::move(entity_type)},
          logger_{std::move(logger)} {}

  private:
    nlohmann::json json_;
    EntityName id_;
    std::string entity_type_;
    AsyncMapConsumerLoggerPtr logger_;
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP
