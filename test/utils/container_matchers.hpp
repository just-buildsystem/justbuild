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

#ifndef INCLUDED_SRC_TEST_UTILS_CONTAINER_MATCHERS_HPP
#define INCLUDED_SRC_TEST_UTILS_CONTAINER_MATCHERS_HPP

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "catch2/matchers/catch_matchers_all.hpp"

/// \brief Matcher to check if the sets of elements present in two different
/// containers are the same
template <class LeftContainer, class RightContainer>
class UniqueElementsUnorderedMatcher
    : public Catch::Matchers::MatcherBase<LeftContainer> {

    RightContainer rhs_;

  public:
    // Note that in the case of an associative container with type C,
    // C::value_type == std::pair<C::key_type const, C::mapped_type>.
    // That would be a problem as we will be using
    // std::unordered_set<C::value_type> So don't use this class in the case you
    // want to compare two (multi)maps (ordered/unordered)
    using value_type = typename LeftContainer::value_type;
    using T = value_type;
    static_assert(
        std::is_constructible_v<T, typename RightContainer::value_type>,
        "Value type of container in the left hand side must be constructible "
        "from that of the right hand side.");

    explicit UniqueElementsUnorderedMatcher(RightContainer const& rc)
        : rhs_(rc) {}

    UniqueElementsUnorderedMatcher() = delete;

    // Method that produces the result to be evaluated
    [[nodiscard]] auto match(LeftContainer const& lc) const -> bool override {
        return IsEqualToRHS(
            std::unordered_set<T>(std::begin(lc), std::end(lc)));
    }

    [[nodiscard]] auto describe() const -> std::string override {
        std::ostringstream ss;
        ss << "\nhas the same unique elements as\n{";
        auto elem_it = std::begin(rhs_);
        if (elem_it != std::end(rhs_)) {
            ss << *elem_it;
            ++elem_it;
            for (; elem_it != std::end(rhs_); ++elem_it) {
                ss << ", " << *elem_it;
            }
        }
        ss << "}.";
        return ss.str();
    }

  private:
    [[nodiscard]] auto IsEqualToRHS(std::unordered_set<T> const& lhs) const
        -> bool {
        std::unordered_set<T> rhs(std::begin(rhs_), std::end(rhs_));
        for (auto const& elem : lhs) {
            auto elem_it_rhs = rhs.find(elem);
            if (elem_it_rhs == std::end(rhs)) {
                return false;
            }
            rhs.erase(elem_it_rhs);
        }
        return rhs.empty();
    }
};

template <class LeftContainer, class RightContainer>
inline auto HasSameUniqueElementsAs(RightContainer const& rc)
    -> UniqueElementsUnorderedMatcher<LeftContainer, RightContainer> {
    return UniqueElementsUnorderedMatcher<LeftContainer, RightContainer>(rc);
}

template <class LeftContainer, class T>
inline auto HasSameUniqueElementsAs(std::initializer_list<T> const& rc)
    -> UniqueElementsUnorderedMatcher<LeftContainer, std::initializer_list<T>> {
    return UniqueElementsUnorderedMatcher<LeftContainer,
                                          std::initializer_list<T>>(rc);
}

/// \brief Matcher to compare the contents of two containers up to permutation
template <class LeftContainer>
class ContainerUnorderedMatcher
    : public Catch::Matchers::MatcherBase<LeftContainer> {
  public:
    using value_type = typename LeftContainer::value_type;
    using T = value_type;

    explicit ContainerUnorderedMatcher(std::vector<T> const& rc) : rhs_(rc) {}

    ContainerUnorderedMatcher() = delete;

    // Method that produces the result to be evaluated
    [[nodiscard]] auto match(LeftContainer const& lc) const -> bool override {
        return IsEqualToRHS(std::vector<T>(std::begin(lc), std::end(lc)));
    }

    [[nodiscard]] auto describe() const -> std::string override {
        std::ostringstream ss;
        ss << "\nhas the same elements as\n{";
        auto elem_it = std::begin(rhs_);
        if (elem_it != std::end(rhs_)) {
            ss << *elem_it;
            ++elem_it;
            for (; elem_it != std::end(rhs_); ++elem_it) {
                ss << ", " << *elem_it;
            }
        }
        ss << "}.";
        return ss.str();
    }

  private:
    std::vector<T> rhs_;

    /// \brief Compare containers by checking they have the same elements
    /// (repetitions included). This implementation is not optimal, but it
    /// doesn't require that the type T = LeftContainer::value_type has
    /// known-to-STL hashing function or partial order (<)
    [[nodiscard]] auto IsEqualToRHS(std::vector<T> const& lhs) const -> bool {
        if (std::size(lhs) != std::size(rhs_)) {
            return false;
        }

        // Get iterators to the rhs vector, we will remove iterators of elements
        // found from this vector in order to account for repetitions
        std::vector<typename std::vector<T>::const_iterator> iterators_to_check(
            rhs_.size());
        std::iota(std::begin(iterators_to_check),
                  std::end(iterators_to_check),
                  std::begin(rhs_));

        // Instead of removing elements from the vector, as this would mean
        // moving O(n) of them, we swap them to the back of the vector and keep
        // track of what's the last element that has to be checked.
        // This is similar to std::remove, but we are only interested in doing
        // it for one element at a time.
        auto last_to_check = std::end(iterators_to_check);
        auto check_exists_and_remove = [&iterators_to_check,
                                        &last_to_check](T const& elem) {
            auto it_to_elem =
                std::find_if(std::begin(iterators_to_check),
                             last_to_check,
                             [&elem](auto iter) { return *iter == elem; });
            if (it_to_elem == last_to_check) {
                return false;
            }
            --last_to_check;
            std::iter_swap(it_to_elem, last_to_check);
            return true;
        };
        return std::all_of(lhs.begin(),
                           lhs.end(),
                           [&check_exists_and_remove](auto const& element) {
                               return check_exists_and_remove(element);
                           });
    }
};

template <class LeftContainer>
inline auto HasSameElementsAs(
    std::vector<typename LeftContainer::value_type> const& rc)
    -> ContainerUnorderedMatcher<LeftContainer> {
    return ContainerUnorderedMatcher<LeftContainer>(rc);
}

#endif  // INCLUDED_SRC_TEST_UTILS_CONTAINER_MATCHERS_HPP
