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

#ifndef INCLUDED_SRC_OTHER_TOOLS_TRANSFORMED_RANGE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_TRANSFORMED_RANGE_HPP

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>  //std::move
#include <vector>

#include "gsl/gsl"

// TODO(modernize): Replace any use of this class by C++20's
// std::ranges/std::views (clang version >= 16.0.0)

/// \brief Transform iterable sequence "on the fly" invoking the given
/// transformation callback. If the callback throws an exception,
/// std::terminate is called.
/// \tparam Iterator        Type of the iterator of the sequence to be
/// transformed.
/// \tparam Result          Type of the transformation result.
template <typename Iterator, typename Result>
class TransformedRange final {
  public:
    using converter_t =
        std::function<Result(typename Iterator::value_type const&)>;

    class iterator final {
      public:
        using value_type = std::remove_reference_t<Result>;
        using pointer = value_type*;
        using reference = value_type&;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        iterator() noexcept = default;
        iterator(Iterator iterator, converter_t c) noexcept
            : iterator_(std::move(iterator)), c_(std::move(c)) {}

        auto operator*() const noexcept -> decltype(auto) {
            try {
                return std::invoke(c_, *iterator_);
            } catch (...) {
                Ensures(false);
            }
        }

        auto operator++() noexcept -> iterator& {
            ++iterator_;
            return *this;
        }

        [[nodiscard]] friend auto operator==(iterator const& lhs,
                                             iterator const& rhs) noexcept
            -> bool {
            return lhs.iterator_ == rhs.iterator_;
        }

        [[nodiscard]] friend auto operator!=(iterator const& lhs,
                                             iterator const& rhs) noexcept
            -> bool {
            return not(lhs == rhs);
        }

      private:
        Iterator iterator_{};
        converter_t c_{};
    };

    TransformedRange(Iterator begin, Iterator end, converter_t c) noexcept
        : begin_{std::move(begin), std::move(c)},
          end_{std::move(end), nullptr} {}

    [[nodiscard]] auto begin() const noexcept -> iterator { return begin_; }
    [[nodiscard]] auto end() const noexcept -> iterator { return end_; }
    [[nodiscard]] auto size() const -> typename iterator::difference_type {
        return std::distance(begin_, end_);
    }

    [[nodiscard]] auto ToVector() const -> std::vector<Result> {
        std::vector<Result> result;
        result.reserve(size());
        for (auto item : *this) {
            result.emplace_back(std::move(item));
        }
        return result;
    }

  private:
    iterator const begin_;
    iterator const end_;
};

// User-defined deduction guide to help compiler dealing with generic lambdas
// and invokable objects.
template <typename Iterator,
          typename Function,
          typename IteratorValue = typename Iterator::value_type>
TransformedRange(Iterator, Iterator, Function)
    -> TransformedRange<Iterator,
                        std::invoke_result_t<Function, IteratorValue>>;

#endif  // INCLUDED_SRC_OTHER_TOOLS_TRANSFORMED_RANGE_HPP
