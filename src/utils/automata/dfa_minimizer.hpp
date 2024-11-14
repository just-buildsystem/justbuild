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

#ifndef INCLUDED_SRC_UTILS_AUTOMATA_DFA_MINIMIZER_HPP
#define INCLUDED_SRC_UTILS_AUTOMATA_DFA_MINIMIZER_HPP

#include <algorithm>
#include <compare>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/hex_string.hpp"

// Minimizes a DFA by separating distinguishable states. States added with same
// content id are considered initially indistinguishable. The algorithm has
// complexity O(n^2) among each set of initially indistinguishable states.
// Note that for incomplete graphs, two states are considered distinguishable if
// they transition for the same symbol to two differently named non-existing
// states. This is done for efficiency reasons, as we then can avoid creating an
// additional bucket for non-existing states. This is sufficient for our use
// case, as we are only interested in the bisimulation of states in complete
// graphs.
class DFAMinimizer {
    // Maps symbols to states
    using transitions_t = std::map<std::string, std::string>;
    using states_t = std::unordered_map<std::string, transitions_t>;

    // Bucket of states with equal local properties (content and acceptance)
    struct Bucket {
        std::vector<std::string> symbols;
        states_t states;
    };

    // Key used for state pairs. Reordering names will result in the same key.
    class StatePairKey {
      public:
        struct Hash {
            [[nodiscard]] auto operator()(StatePairKey const& p) const
                -> std::size_t {
                std::size_t hash{};
                hash_combine(&hash, p.Names().first);
                hash_combine(&hash, p.Names().second);
                return hash;
            }
        };

        StatePairKey(std::string first, std::string second) {
            if (first < second) {
                data_ = std::make_pair(std::move(first), std::move(second));
            }
            else {
                data_ = std::make_pair(std::move(second), std::move(first));
            }
        }
        [[nodiscard]] auto Names()
            const& -> std::pair<std::string, std::string> const& {
            return data_;
        }
        [[nodiscard]] auto operator==(StatePairKey const& other) const
            -> bool = default;
        [[nodiscard]] auto operator!=(StatePairKey const& other) const
            -> bool = default;

      private:
        std::pair<std::string, std::string> data_;
    };

    // Value of state pairs.
    struct StatePairValue {
        // Parent pairs depending on this pair's distinguishability
        std::vector<StatePairValue*> parents;
        // Distinguishability flag (true means distinguishable)
        bool marked{};
    };

    using state_pairs_t =
        std::unordered_map<StatePairKey, StatePairValue, StatePairKey::Hash>;

  public:
    using bisimulation_t = std::unordered_map<std::string, std::string>;

    // Add state with name, transitions, and content id. States with the same
    // content id are initially considered indistinguishable.
    void AddState(std::string const& name,
                  transitions_t const& transitions,
                  std::string const& content_id = {}) {
        auto symbols = GetKeys(transitions);

        // Compute bucket id from content id and transition symbols
        auto bucket_id =
            nlohmann::json(std::pair{ToHexString(content_id), symbols}).dump();

        // Store indistinguishable states in same bucket
        auto it = buckets_.find(bucket_id);
        if (it == buckets_.end()) {
            it = buckets_.emplace(bucket_id, Bucket{std::move(symbols)}).first;
        }
        it->second.states.emplace(name, transitions);
        Ensures(buckets_by_state_.emplace(name, bucket_id).second);
    }

    // Compute bisimulation for each state and return a map, which maps a
    // state name to its bisimulation if one was found.
    [[nodiscard]] auto ComputeBisimulation() const -> bisimulation_t {
        auto pairs = CreatePairs();

        // Mark pairs of distinguishable states
        for (auto& [key, ab_val] : pairs) {
            auto const& [a, b] = key.Names();
            auto const& bucket_id = buckets_by_state_.at(a);
            auto const& bucket = buckets_.at(bucket_id);
            for (auto const& sym : bucket.symbols) {
                auto const& r = bucket.states.at(a).at(sym);
                auto const& s = bucket.states.at(b).at(sym);
                if (r != s) {
                    auto* rs_val = LookupPairValue(&pairs, {r, s});
                    if (rs_val == nullptr or rs_val->marked) {
                        // if rs_val == nullptr, the pair is missing, due to:
                        // - both do not exist (and are named differently)
                        // - either r or s does not exist
                        // - r and s do exist but are not in the same bucket
                        MarkPairValue(&ab_val);
                    }
                    else if (rs_val != nullptr) {
                        // Remember ab_val if rs_val ever gets marked
                        rs_val->parents.emplace_back(&ab_val);
                    }
                }
            }
        }

        // Compute the bisimulation for each state
        bisimulation_t bisimulation{};
        for (auto const& [_, bucket] : buckets_) {
            // Consider states in unmarked pairs to be equivalent
            auto all_states = GetKeys(bucket.states);
            while (not all_states.empty()) {
                auto last = std::move(all_states.back());
                all_states.pop_back();
                auto unequal_states = all_states;
                for (auto const& state : all_states) {
                    // Lookup is safe, both must be in the same bucket and
                    // therefore the pair is are guaranteed to exist
                    if (not LookupPairValue(&pairs, {last, state})->marked) {
                        bisimulation.emplace(state, last);
                        std::erase(unequal_states, state);
                    }
                }
                all_states = unequal_states;
            }
        }
        return bisimulation;
    }

  private:
    std::unordered_map<std::string, Bucket> buckets_;
    std::unordered_map<std::string, std::string> buckets_by_state_;

    template <class M, class K = typename M::key_type>
    [[nodiscard]] static auto GetKeys(M const& map) -> std::vector<K> {
        auto keys = std::vector<K>{};
        keys.reserve(map.size());
        std::transform(map.begin(),
                       map.end(),
                       std::back_inserter(keys),
                       [](auto const& kv) { return kv.first; });
        return keys;
    }

    // Returns nullptr if no pair with given key was found.
    [[nodiscard]] static auto LookupPairValue(
        gsl::not_null<state_pairs_t*> const& pairs,
        StatePairKey const& key) -> StatePairValue* {
        auto it = pairs->find(key);
        if (it != pairs->end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Mark pair as distinguishable and recursively mark all parents.
    static void MarkPairValue(gsl::not_null<StatePairValue*> const& data) {
        data->marked = true;
        for (auto* parent : data->parents) {
            if (not parent->marked) {
                MarkPairValue(parent);
            }
        }
    }

    // Create n to n pairs for all states in the same bucket.
    [[nodiscard]] auto CreatePairs() const -> state_pairs_t {
        state_pairs_t pairs{};
        for (auto const& [_, bucket] : buckets_) {
            auto const& states = bucket.states;
            auto const n = states.size();
            pairs.reserve(pairs.size() + ((n * (n - 1)) / 2));

            auto const end = states.end();
            for (auto it = ++states.begin(); it != end; ++it) {
                for (auto it2 = states.begin(); it2 != it; ++it2) {
                    pairs.emplace(StatePairKey{it->first, it2->first},
                                  StatePairValue{});
                }
            }
        }
        return pairs;
    }
};

#endif  // INCLUDED_SRC_UTILS_AUTOMATA_DFA_MINIMIZER_HPP
