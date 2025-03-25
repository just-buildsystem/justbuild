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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP

#include <algorithm>
#include <compare>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // std::move
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/analysed_target/target_graph_information.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/action_description.hpp"
#include "src/buildtool/common/identifier.hpp"
#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/common/tree_overlay.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"
#include "src/buildtool/storage/target_cache_key.hpp"

namespace BuildMaps::Target {

// Class collecting analysed targets for their canonical configuration.
class ResultTargetMap {
  public:
    struct ActionWithOrigin {
        ActionDescription::Ptr desc;
        nlohmann::json origin;
    };

    template <bool kIncludeOrigins = false>
    struct ResultType {
        std::vector<ActionDescription::Ptr> actions;
        std::vector<std::string> blobs;
        std::vector<Tree::Ptr> trees;
        std::vector<TreeOverlay::Ptr> tree_overlays;
    };

    explicit ResultTargetMap(std::size_t jobs) : width_{ComputeWidth(jobs)} {}

    ResultTargetMap() = default;

    // \brief Add the analysed target for the given target and
    // configuration, if no entry is present for the given
    // target-configuration pair. \returns the analysed target that is
    // element of the map after insertion.
    [[nodiscard]] auto Add(
        BuildMaps::Base::EntityName name,
        Configuration conf,
        gsl::not_null<AnalysedTargetPtr> const& result,
        std::optional<TargetCacheKey> target_cache_key = std::nullopt,
        bool is_export_target = false) -> AnalysedTargetPtr {
        auto part = std::hash<BuildMaps::Base::EntityName>{}(name) % width_;
        std::unique_lock lock{m_[part]};
        auto [entry, inserted] =
            targets_[part].emplace(ConfiguredTarget{.target = std::move(name),
                                                    .config = std::move(conf)},
                                   result);
        if (target_cache_key) {
            cache_targets_[part].emplace(*target_cache_key, entry->second);
        }
        if (is_export_target) {
            export_targets_[part].emplace(entry->first);
        }
        if (inserted) {
            num_actions_[part] += entry->second->Actions().size();
            num_blobs_[part] += entry->second->Blobs().size();
            num_trees_[part] += entry->second->Trees().size();
        }
        return entry->second;
    }

    [[nodiscard]] auto ConfiguredTargets() const noexcept
        -> std::vector<ConfiguredTarget> {
        std::vector<ConfiguredTarget> targets{};
        std::size_t s = 0;
        for (const auto& target : targets_) {
            s += target.size();
        }
        targets.reserve(s);
        for (const auto& i : targets_) {
            std::transform(i.begin(),
                           i.end(),
                           std::back_inserter(targets),
                           [](auto const& target) { return target.first; });
        }
        std::sort(targets.begin(),
                  targets.end(),
                  [](auto const& lhs, auto const& rhs) {
                      return lhs.ToString() < rhs.ToString();
                  });
        return targets;
    }

    [[nodiscard]] auto ExportTargets() const noexcept
        -> std::vector<ConfiguredTarget> {
        std::vector<ConfiguredTarget> all_exports{};
        for (auto const& exports : export_targets_) {
            all_exports.insert(
                all_exports.end(), exports.begin(), exports.end());
        }
        std::sort(all_exports.begin(),
                  all_exports.end(),
                  [](auto const& lhs, auto const& rhs) {
                      return lhs.ToString() < rhs.ToString();
                  });
        return all_exports;
    }

    [[nodiscard]] auto ConfiguredTargetsGraph() const -> nlohmann::json {
        auto result = nlohmann::json::object();
        for (auto const& i : targets_) {
            for (auto const& it : i) {
                auto const& info = it.second->GraphInformation();
                auto node = info.NodeString();
                if (node) {
                    result[*node] = info.DepsToJson();
                }
            }
        }
        return result;
    }

    [[nodiscard]] auto CacheTargets() const noexcept
        -> std::unordered_map<TargetCacheKey, AnalysedTargetPtr> {
        return std::accumulate(
            cache_targets_.begin(),
            cache_targets_.end(),
            std::unordered_map<TargetCacheKey, AnalysedTargetPtr>{},
            [](auto&& l, auto const& r) {
                l.insert(r.begin(), r.end());
                return std::forward<decltype(l)>(l);
            });
    }

    [[nodiscard]] auto GetAction(const ActionIdentifier& identifier)
        -> std::optional<ActionDescription::Ptr> {
        for (const auto& target : targets_) {
            for (const auto& el : target) {
                for (const auto& action : el.second->Actions()) {
                    if (action->Id() == identifier) {
                        return action;
                    }
                }
            }
        }
        return std::nullopt;
    }

    template <bool kIncludeOrigins = false>
    [[nodiscard]] auto ToResult(gsl::not_null<Statistics const*> const& stats,
                                gsl::not_null<Progress*> const& progress,
                                Logger const* logger = nullptr) const
        -> ResultType<kIncludeOrigins> {
        ResultType<kIncludeOrigins> result{};
        std::size_t na = 0;
        std::size_t nb = 0;
        std::size_t nt = 0;
        for (std::size_t i = 0; i < width_; i++) {
            na += num_actions_[i];
            nb += num_blobs_[i];
            nt += num_trees_[i];
        }
        result.actions.reserve(na);
        result.blobs.reserve(nb);
        result.trees.reserve(nt);

        auto& origin_map = progress->OriginMap();
        origin_map.clear();
        origin_map.reserve(na);
        for (const auto& target : targets_) {
            std::for_each(target.begin(), target.end(), [&](auto const& el) {
                auto const& actions = el.second->Actions();
                std::size_t pos{};
                std::for_each(
                    actions.begin(),
                    actions.end(),
                    [&origin_map, &pos, &el](auto const& action) {
                        std::pair<ConfiguredTarget, std::size_t> origin{
                            el.first, pos++};
                        auto id = action->Id();
                        if (origin_map.contains(id)) {
                            origin_map[id].push_back(origin);
                        }
                        else {
                            origin_map[id] = std::vector<
                                std::pair<ConfiguredTarget, std::size_t>>{
                                origin};
                        }
                    });
            });
            // Sort origins to get a reproducible order. We don't expect many
            // origins for a single action, so the cost of comparison is not
            // too important. Moreover, we expect most actions to have a single
            // origin, so any precomputation would be more expensive.
            for (auto const& i : origin_map) {
                std::sort(origin_map[i.first].begin(),
                          origin_map[i.first].end(),
                          [](auto const& left, auto const& right) {
                              auto const& left_target = left.first;
                              auto const& right_target = right.first;
                              return (left_target < right_target) or
                                     (left_target == right_target and
                                      left.second < right.second);
                          });
            }
        }

        for (const auto& target : targets_) {
            std::for_each(target.begin(), target.end(), [&](auto const& el) {
                auto const& actions = el.second->Actions();
                if constexpr (kIncludeOrigins) {
                    std::for_each(
                        actions.begin(),
                        actions.end(),
                        [&result, &origin_map](auto const& action) {
                            auto origins = nlohmann::json::array();
                            for (auto const& [ct, count] :
                                 origin_map[action->Id()]) {
                                origins.push_back(nlohmann::json{
                                    {"target", ct.target.ToJson()},
                                    {"subtask", count},
                                    {"config", ct.config.ToJson()}});
                            }
                            result.actions.emplace_back(ActionWithOrigin{
                                .desc = action, .origin = origins});
                        });
                }
                else {
                    std::for_each(actions.begin(),
                                  actions.end(),
                                  [&result](auto const& action) {
                                      result.actions.emplace_back(action);
                                  });
                }
                auto const& blobs = el.second->Blobs();
                auto const& trees = el.second->Trees();
                result.blobs.insert(
                    result.blobs.end(), blobs.begin(), blobs.end());
                result.trees.insert(
                    result.trees.end(), trees.begin(), trees.end());
            });
        }

        std::sort(result.blobs.begin(), result.blobs.end());
        auto lastblob = std::unique(result.blobs.begin(), result.blobs.end());
        result.blobs.erase(lastblob, result.blobs.end());

        std::sort(
            result.trees.begin(),
            result.trees.end(),
            [](auto left, auto right) { return left->Id() < right->Id(); });
        auto lasttree = std::unique(
            result.trees.begin(),
            result.trees.end(),
            [](auto left, auto right) { return left->Id() == right->Id(); });
        result.trees.erase(lasttree, result.trees.end());

        std::sort(result.actions.begin(),
                  result.actions.end(),
                  [](auto left, auto right) {
                      if constexpr (kIncludeOrigins) {
                          return left.desc->Id() < right.desc->Id();
                      }
                      else {
                          return left->Id() < right->Id();
                      }
                  });
        auto lastaction =
            std::unique(result.actions.begin(),
                        result.actions.end(),
                        [](auto left, auto right) {
                            if constexpr (kIncludeOrigins) {
                                return left.desc->Id() == right.desc->Id();
                            }
                            else {
                                return left->Id() == right->Id();
                            }
                        });
        result.actions.erase(lastaction, result.actions.end());
        auto& output_map = progress->OutputMap();
        output_map.clear();
        output_map.reserve(na);
        if constexpr (kIncludeOrigins) {
            for (auto const& action : result.actions) {
                if (not action.desc->OutputFiles().empty()) {
                    output_map[action.desc->Id()] =
                        action.desc->OutputFiles()[0];
                }
                else if (not action.desc->OutputDirs().empty()) {
                    output_map[action.desc->Id()] =
                        action.desc->OutputDirs()[0];
                }
            }
        }
        else {
            for (auto const& action : result.actions) {
                if (not action->OutputFiles().empty()) {
                    output_map[action->Id()] = action->OutputFiles()[0];
                }
                else if (not action->OutputDirs().empty()) {
                    output_map[action->Id()] = action->OutputDirs()[0];
                }
            }
        }

        int trees_traversed = stats->TreesAnalysedCounter();
        if (trees_traversed > 0) {
            Logger::Log(logger,
                        LogLevel::Performance,
                        "Analysed {} non-known source trees",
                        trees_traversed);
        }
        Logger::Log(logger,
                    LogLevel::Info,
                    "Discovered {} actions, {} trees, {} blobs",
                    result.actions.size(),
                    result.trees.size(),
                    result.blobs.size());

        return result;
    }

    template <bool kIncludeOrigins = false>
    [[nodiscard]] auto ToJson(gsl::not_null<Statistics const*> const& stats,
                              gsl::not_null<Progress*> const& progress,
                              Logger const* logger = nullptr) const
        -> nlohmann::json {
        auto const result = ToResult<kIncludeOrigins>(stats, progress, logger);
        auto actions = nlohmann::json::object();
        auto trees = nlohmann::json::object();
        std::for_each(result.actions.begin(),
                      result.actions.end(),
                      [&actions](auto const& action) {
                          if constexpr (kIncludeOrigins) {
                              auto const& id = action.desc->GraphAction().Id();
                              actions[id] = action.desc->ToJson();
                              actions[id]["origins"] = action.origin;
                          }
                          else {
                              auto const& id = action->GraphAction().Id();
                              actions[id] = action->ToJson();
                          }
                      });
        std::for_each(
            result.trees.begin(),
            result.trees.end(),
            [&trees](auto const& tree) { trees[tree->Id()] = tree->ToJson(); });
        return nlohmann::json{
            {"actions", actions}, {"blobs", result.blobs}, {"trees", trees}};
    }

    template <bool kIncludeOrigins = true>
    auto ToFile(std::vector<std::filesystem::path> const& destinations,
                gsl::not_null<Statistics const*> const& stats,
                gsl::not_null<Progress*> const& progress,
                int indent = 2) const -> void {
        auto logger = Logger("result-dumping");
        logger.SetLogLimit(LogLevel::Error);
        if (not destinations.empty()) {
            // As serialization is expensive, compute the string only once
            auto data =
                ToJson<kIncludeOrigins>(stats, progress, &logger).dump(indent);
            for (auto const& graph_file : destinations) {
                Logger::Log(LogLevel::Info,
                            "Dumping action graph to file {}.",
                            graph_file.string());
                std::ofstream os(graph_file);
                os << data << std::endl;
            }
        }
    }

    void Clear(gsl::not_null<TaskSystem*> const& ts) {
        for (std::size_t i = 0; i < width_; ++i) {
            ts->QueueTask([i, this]() {
                targets_[i].clear();
                cache_targets_[i].clear();
            });
        }
    }

  private:
    constexpr static std::size_t kScalingFactor = 2;
    std::size_t width_{ComputeWidth(0)};
    std::vector<std::mutex> m_{width_};
    std::vector<
        std::unordered_map<ConfiguredTarget, gsl::not_null<AnalysedTargetPtr>>>
        targets_{width_};
    std::vector<
        std::unordered_map<TargetCacheKey, gsl::not_null<AnalysedTargetPtr>>>
        cache_targets_{width_};
    std::vector<std::unordered_set<ConfiguredTarget>> export_targets_{width_};
    std::vector<std::size_t> num_actions_{std::vector<std::size_t>(width_)};
    std::vector<std::size_t> num_blobs_{std::vector<std::size_t>(width_)};
    std::vector<std::size_t> num_trees_{std::vector<std::size_t>(width_)};

    constexpr static auto ComputeWidth(std::size_t jobs) -> std::size_t {
        if (jobs <= 0) {
            // Non-positive indicates to use the default value
            return ComputeWidth(
                std::max(1U, std::thread::hardware_concurrency()));
        }
        return jobs * kScalingFactor + 1;
    }

};  // namespace BuildMaps::Target

template <>
struct ResultTargetMap::ResultType</*kIncludeOrigins=*/true> {
    std::vector<ActionWithOrigin> actions;
    std::vector<std::string> blobs;
    std::vector<Tree::Ptr> trees;
    std::vector<TreeOverlay::Ptr> tree_overlays;
};

}  // namespace BuildMaps::Target

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP
