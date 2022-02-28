#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP

#include <algorithm>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/tree.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/multithreading/task.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/hash_combine.hpp"

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
        std::vector<ActionDescription::Ptr> actions{};
        std::vector<std::string> blobs{};
        std::vector<Tree::Ptr> trees{};
    };

    template <>
    struct ResultType</*kIncludeOrigins=*/true> {
        std::vector<ActionWithOrigin> actions{};
        std::vector<std::string> blobs{};
        std::vector<Tree::Ptr> trees{};
    };

    explicit ResultTargetMap(std::size_t jobs) : width_{ComputeWidth(jobs)} {}

    ResultTargetMap() = default;

    // \brief Add the analysed target for the given target and
    // configuration, if no entry is present for the given
    // target-configuration pair. \returns the analysed target that is
    // element of the map after insertion.
    [[nodiscard]] auto Add(BuildMaps::Base::EntityName name,
                           Configuration conf,
                           gsl::not_null<AnalysedTargetPtr> result)
        -> AnalysedTargetPtr {
        auto part = std::hash<BuildMaps::Base::EntityName>{}(name) % width_;
        std::unique_lock lock{m_[part]};
        auto [entry, inserted] = targets_[part].emplace(
            ConfiguredTarget{std::move(name), std::move(conf)},
            std::move(result));
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
        size_t s = 0;
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

    template <bool kIncludeOrigins = false>
    [[nodiscard]] auto ToResult() const -> ResultType<kIncludeOrigins> {
        ResultType<kIncludeOrigins> result{};
        size_t na = 0;
        size_t nb = 0;
        size_t nt = 0;
        for (std::size_t i = 0; i < width_; i++) {
            na += num_actions_[i];
            nb += num_blobs_[i];
            nt += num_trees_[i];
        }
        result.actions.reserve(na);
        result.blobs.reserve(nb);
        result.trees.reserve(nt);

        std::unordered_map<
            std::string,
            std::vector<std::pair<ConfiguredTarget, std::size_t>>>
            origin_map;
        origin_map.reserve(na);
        if constexpr (kIncludeOrigins) {
            for (const auto& target : targets_) {
                std::for_each(
                    target.begin(), target.end(), [&](auto const& el) {
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
                                    origin_map[id] =
                                        std::vector<std::pair<ConfiguredTarget,
                                                              std::size_t>>{
                                            origin};
                                }
                            });
                    });
            }
            // Sort origins to get a reproducible order. We don't expect many
            // origins for a single action, so the cost of comparison is not
            // too important. Moreover, we expect most actions to have a single
            // origin, so any precomputation would be more expensive.
            for (auto const& i : origin_map) {
                std::sort(origin_map[i.first].begin(),
                          origin_map[i.first].end(),
                          [](auto const& left, auto const& right) {
                              auto left_target = left.first.ToString();
                              auto right_target = right.first.ToString();
                              return (left_target < right_target) ||
                                     (left_target == right_target &&
                                      left.second < right.second);
                          });
            }
        }

        for (const auto& target : targets_) {
            std::for_each(target.begin(), target.end(), [&](auto const& el) {
                auto const& actions = el.second->Actions();
                if constexpr (kIncludeOrigins) {
                    std::for_each(actions.begin(),
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
                                      result.actions.emplace_back(
                                          ActionWithOrigin{action, origins});
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

        Logger::Log(LogLevel::Info,
                    "Discovered {} actions, {} trees, {} blobs",
                    result.actions.size(),
                    result.trees.size(),
                    result.blobs.size());

        return result;
    }

    template <bool kIncludeOrigins = false>
    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        auto const result = ToResult<kIncludeOrigins>();
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
    auto ToFile(std::string const& graph_file, int indent = 2) const -> void {
        std::ofstream os(graph_file);
        os << std::setw(indent) << ToJson<kIncludeOrigins>() << std::endl;
    }

    void Clear(gsl::not_null<TaskSystem*> const& ts) {
        for (std::size_t i = 0; i < width_; ++i) {
            ts->QueueTask([i, this]() { targets_[i].clear(); });
        }
    }

  private:
    constexpr static std::size_t kScalingFactor = 2;
    std::size_t width_{ComputeWidth(0)};
    std::vector<std::mutex> m_{width_};
    std::vector<
        std::unordered_map<ConfiguredTarget, gsl::not_null<AnalysedTargetPtr>>>
        targets_{width_};
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

}  // namespace BuildMaps::Target

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_RESULT_MAP_HPP
