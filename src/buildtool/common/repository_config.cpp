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

#include "src/buildtool/common/repository_config.hpp"

#include "src/utils/automata/dfa_minimizer.hpp"

auto RepositoryConfig::RepositoryInfo::BaseContentDescription() const
    -> std::optional<nlohmann::json> {
    auto wroot = workspace_root.ContentDescription();
    auto troot = target_root.ContentDescription();
    auto rroot = rule_root.ContentDescription();
    auto eroot = expression_root.ContentDescription();
    if (wroot and troot and rroot and eroot) {
        return nlohmann::json{{"workspace_root", *wroot},
                              {"target_root", *troot},
                              {"rule_root", *rroot},
                              {"expression_root", *eroot},
                              {"target_file_name", target_file_name},
                              {"rule_file_name", rule_file_name},
                              {"expression_file_name", expression_file_name}};
    }
    return std::nullopt;
}

auto RepositoryConfig::RepositoryKey(Storage const& storage,
                                     std::string const& repo) const noexcept
    -> std::optional<ArtifactDigest> {
    auto const unique = DeduplicateRepo(repo, storage.GetHashFunction());
    if (auto const* data = Data(unique)) {
        // compute key only once (thread-safe)
        return data->key.SetOnceAndGet(
            [this, &storage, &unique]() -> std::optional<ArtifactDigest> {
                if (auto graph = BuildGraphForRepository(
                        unique, storage.GetHashFunction())) {
                    auto const& cas = storage.CAS();
                    return cas.StoreBlob(graph->dump(2));
                }
                return std::nullopt;
            });
    }
    return std::nullopt;
}

// Obtain canonical name (according to bisimulation) for the given repository.
auto RepositoryConfig::DeduplicateRepo(std::string const& repo,
                                       HashFunction hash_function) const
    -> std::string {
    // Compute duplicates map only once (thread-safe)
    auto const& duplicates = duplicates_.SetOnceAndGet([this, &hash_function] {
        // To detect duplicate repository descriptions, we represent each
        // repository as a DFA state with repo name as state name, repo
        // bindings as state transitions, and repo base description as state
        // content id. Then we use a DFA minimizer to find the bisimulation
        // for each state.
        auto minimizer = DFAMinimizer{};
        for (auto const& [repo, data] : repos_) {
            // Only add content-fixed repositories. This is sufficient, as for
            // incomplete graphs our minimizer implementation identifies states
            // with transitions to differently-named missing states as
            // distinguishable. Even if those were considered indistinguishable,
            // repository key computation would still work correctly, as this
            // computation is only performed if all transitive dependencies are
            // content-fixed.
            if (data.base_desc) {
                // Use hash of content-fixed base description as content id
                auto hash =
                    hash_function.PlainHashData(data.base_desc->dump()).Bytes();
                // Add state with name, transitions, and content id
                minimizer.AddState(repo, data.info.name_mapping, hash);
            }
        }
        return minimizer.ComputeBisimulation();
    });

    // Lookup canonical name for given repository in duplicates map
    auto it = duplicates.find(repo);
    if (it != duplicates.end()) {
        return it->second;
    }
    return repo;
}

// Returns the repository-local representation of its dependency graph if all
// contained repositories are content fixed or return std::nullopt otherwise.
auto RepositoryConfig::BuildGraphForRepository(std::string const& repo,
                                               HashFunction hash_function) const
    -> std::optional<nlohmann::json> {
    auto graph = nlohmann::json::object();
    int id_counter{};
    std::unordered_map<std::string, std::string> repo_ids{};
    if (AddToGraphAndGetId(
            &graph, &id_counter, &repo_ids, repo, hash_function)) {
        return graph;
    }
    return std::nullopt;
}

// Add given repository to the given graph and return its traversal specific
// unique id if it and all its dependencies are content-fixed or return
// std::nullopt otherwise. Recursion immediately aborts on traversing the first
// non-content-fixed repository.
auto RepositoryConfig::AddToGraphAndGetId(
    gsl::not_null<nlohmann::json*> const& graph,
    gsl::not_null<int*> const& id_counter,
    gsl::not_null<std::unordered_map<std::string, std::string>*> const&
        repo_ids,
    std::string const& repo,
    HashFunction hash_function) const -> std::optional<std::string> {
    auto const unique_repo = DeduplicateRepo(repo, hash_function);
    auto repo_it = repo_ids->find(unique_repo);
    if (repo_it != repo_ids->end()) {
        // The same or equivalent repository was already requested before
        return repo_it->second;
    }

    auto const* data = Data(unique_repo);
    if (data != nullptr and data->base_desc) {
        // Compute the unique id (traversal order) and store it
        auto repo_id = std::to_string((*id_counter)++);
        repo_ids->emplace(unique_repo, repo_id);

        // Compute repository description from content-fixed base
        // description and bindings to unique ids of depending repositories
        auto repo_desc = *data->base_desc;
        repo_desc["bindings"] = nlohmann::json::object();
        for (auto const& [local_name, global_name] : data->info.name_mapping) {
            auto global_id = AddToGraphAndGetId(
                graph, id_counter, repo_ids, global_name, hash_function);
            if (not global_id) {
                return std::nullopt;
            }
            repo_desc["bindings"][local_name] = std::move(*global_id);
        }

        // Add repository description to graph with unique id as key
        (*graph)[repo_id] = std::move(repo_desc);
        return repo_id;
    }
    return std::nullopt;
}
