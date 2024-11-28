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

#include "src/buildtool/computed_roots/artifacts_root.hpp"

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <stack>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/hex_string.hpp"

namespace {
void AddEntryRaw(gsl::not_null<GitRepo::tree_entries_t*> const& tree,
                 std::string const& p,
                 std::string const& raw_hash,
                 ObjectType const& ot) {
    if (tree->contains(raw_hash)) {
        (*tree)[raw_hash].emplace_back(p, ot);
    }
    else {
        (*tree)[raw_hash] =
            std::vector<GitRepo::TreeEntry>{GitRepo::TreeEntry{p, ot}};
    }
}

auto AddEntry(gsl::not_null<GitRepo::tree_entries_t*> const& tree,
              std::string const& p,
              std::string const& hash,
              ObjectType const& ot,
              AsyncMapConsumerLoggerPtr const& logger) -> bool {
    auto raw_hash = FromHexString(hash);
    if (not raw_hash) {
        (*logger)(
            fmt::format("Not a hex string {}", nlohmann::json(hash).dump()),
            true);
        return false;
    }
    AddEntryRaw(tree, p, *raw_hash, ot);
    return true;
}

// Structure building a git tree from entries traversed in order so that
// all entries of a subdirectory come next to each other.
struct PartialTree {
    std::filesystem::path current_dir_;
    std::stack<GitRepo::tree_entries_t> partial_trees_;

    PartialTree() { partial_trees_.emplace(); }

    void Down(std::string const& segment) {
        if (segment == "." or segment.empty()) {
            return;
        }
        partial_trees_.emplace();
        current_dir_ = current_dir_ / segment;
    }

    auto Up() -> bool {
        auto name = current_dir_.filename().string();
        auto git_tree = GitRepo::CreateShallowTree(partial_trees_.top());
        if (not git_tree) {
            return false;
        }
        current_dir_ = current_dir_.parent_path();
        partial_trees_.pop();
        AddEntryRaw(
            &partial_trees_.top(), name, git_tree->first, ObjectType::Tree);
        return true;
    }

    auto To(std::filesystem::path const& dir) -> bool {
        auto rel_path = dir.lexically_relative(current_dir_);
        while (*rel_path.begin() == "..") {
            if (not Up()) {
                return false;
            }
            rel_path = dir.lexically_relative(current_dir_);
        }
        for (auto const& segment : rel_path) {
            Down(segment.string());
        }
        return true;
    }

    auto Add(std::string const& ps,
             std::string const& hash,
             ObjectType const& ot,
             AsyncMapConsumerLoggerPtr const& logger) -> bool {
        auto p = std::filesystem::path(ps);
        if (not To(p.parent_path())) {
            (*logger)("Failure in tree computation", true);
            return false;
        }
        return AddEntry(
            &partial_trees_.top(), p.filename().string(), hash, ot, logger);
    }

    auto Done(AsyncMapConsumerLoggerPtr const& logger)
        -> std::optional<std::string> {
        while (not current_dir_.empty()) {
            if (not Up()) {
                (*logger)("Failure computing final git tree", true);
                return std::nullopt;
            }
        }
        auto git_tree = GitRepo::CreateShallowTree(partial_trees_.top());
        if (not git_tree) {
            (*logger)("Failure computing top-level git tree", true);
            return std::nullopt;
        }
        return ToHexString(git_tree->first);
    }
};
}  // namespace

auto ArtifactsRoot(ExpressionPtr const& stage,
                   AsyncMapConsumerLoggerPtr const& logger)
    -> std::optional<std::string> {
    if (not stage->IsMap()) {
        (*logger)(fmt::format("Expected stage to be a map, but found {}",
                              stage->ToString()),
                  true);
        return std::nullopt;
    }
    // We obtain the entries odered by key; in particular, the entries of all
    // subtrees will be next to each other. So, we compute the final tree
    // keeping a stack of partially set up tree while walking.
    auto partial_tree = PartialTree();
    for (auto const& [ps, entry] : stage->Map().Items()) {
        if (not entry->IsArtifact()) {
            (*logger)(fmt::format("Expected stage, but at entry {} found {}",
                                  nlohmann::json(ps).dump(),
                                  entry->ToString()),
                      true);
            return std::nullopt;
        }
        auto const& val = entry->Artifact();
        if (not val.IsKnown()) {
            (*logger)(fmt::format(
                          "Expected evaluated stage, but at entry {} found {}",
                          nlohmann::json(ps).dump(),
                          entry->ToString()),
                      true);
            return std::nullopt;
        }
        auto digest = val.ToArtifact().Digest();
        if (not digest) {
            (*logger)(
                fmt::format("Failed to determine digest of known artifact {}",
                            entry->ToString()),
                true);
            return std::nullopt;
        }
        auto ot = val.ToArtifact().Type();
        if (not ot) {
            (*logger)(fmt::format("Failed to determine object typle of known "
                                  "artifact {}",
                                  entry->ToString()),
                      true);
            return std::nullopt;
        }
        if (not partial_tree.Add(ps, digest->hash(), *ot, logger)) {
            return std::nullopt;
        }
    }
    return partial_tree.Done(logger);
}
