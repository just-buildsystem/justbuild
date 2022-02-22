#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/common/identifier.hpp"

class Action {
  public:
    using LocalPath = std::string;

    Action(std::string action_id,
           std::vector<std::string> command,
           std::map<std::string, std::string> env_vars,
           std::optional<std::string> may_fail,
           bool no_cache)
        : id_{std::move(action_id)},
          command_{std::move(command)},
          env_{std::move(env_vars)},
          may_fail_{std::move(may_fail)},
          no_cache_{no_cache} {}

    Action(std::string action_id,
           std::vector<std::string> command,
           std::map<std::string, std::string> env_vars)
        : Action(std::move(action_id),
                 std::move(command),
                 std::move(env_vars),
                 std::nullopt,
                 false) {}

    [[nodiscard]] auto Id() const noexcept -> ActionIdentifier { return id_; }

    [[nodiscard]] auto Command() && noexcept -> std::vector<std::string> {
        return std::move(command_);
    }

    [[nodiscard]] auto Command() const& noexcept
        -> std::vector<std::string> const& {
        return command_;
    }

    [[nodiscard]] auto Env() const& noexcept
        -> std::map<std::string, std::string> {
        return env_;
    }

    [[nodiscard]] auto Env() && noexcept -> std::map<std::string, std::string> {
        return std::move(env_);
    }

    [[nodiscard]] auto IsTreeAction() const -> bool { return is_tree_; }
    [[nodiscard]] auto MayFail() const -> std::optional<std::string> {
        return may_fail_;
    }
    [[nodiscard]] auto NoCache() const -> bool { return no_cache_; }

    [[nodiscard]] static auto CreateTreeAction(ActionIdentifier const& id)
        -> Action {
        return Action{id};
    }

  private:
    ActionIdentifier id_{};
    std::vector<std::string> command_{};
    std::map<std::string, std::string> env_{};
    bool is_tree_{};
    std::optional<std::string> may_fail_{};
    bool no_cache_{};

    explicit Action(ActionIdentifier id) : id_{std::move(id)}, is_tree_{true} {}
};

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ACTION_HPP
