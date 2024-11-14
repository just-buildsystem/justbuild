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
#include <utility>  // IWYU pragma: keep
#include <vector>

#include "gsl/gsl"

// TODO(modernize): Replace any use of this class by C++20's
// std::ranges/std::views (clang version >= 16.0.0)

/// \brief Transform iterable sequence "on the fly" invoking the given
/// transformation callback. If the callback throws an exception,
/// std::terminate is called.
/// \tparam TIterator       Type of the iterator of the sequence to be
/// transformed.
/// \tparam Result          Type of the transformation result.
template <typename TIterator, typename Result>
class TransformedRange final {
  public:
    using converter_t =
        std::function<Result(typename TIterator::value_type const&)>;

    class Iterator final {
      public:
        using value_type = std::remove_reference_t<Result>;
        using pointer = value_type*;
        using reference = value_type&;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        Iterator() noexcept = default;
        Iterator(TIterator Iterator, converter_t c) noexcept
            : iterator_(std::move(Iterator)), c_(std::move(c)) {}

        auto operator*() const noexcept -> decltype(auto) {
            try {
                return std::invoke(c_, *iterator_);
            } catch (...) {
                Ensures(false);
            }
        }

        auto operator++() noexcept -> Iterator& {
            ++iterator_;
            return *this;
        }

        [[nodiscard]] friend auto operator==(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return lhs.iterator_ == rhs.iterator_;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return not(lhs == rhs);
        }

      private:
        TIterator iterator_{};
        converter_t c_{};
    };

    TransformedRange(TIterator begin, TIterator end, converter_t c) noexcept
        : begin_{std::move(begin), std::move(c)},
          end_{std::move(end), nullptr} {}

    [[nodiscard]] auto begin() const noexcept -> Iterator { return begin_; }
    [[nodiscard]] auto end() const noexcept -> Iterator { return end_; }
    [[nodiscard]] auto size() const -> typename Iterator::difference_type {
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
    Iterator const begin_;
    Iterator const end_;
};

// User-defined deduction guide to help compiler dealing with generic lambdas
// and invokable objects.
template <typename TIterator,
          typename Function,
          typename IteratorValue = typename TIterator::value_type>
TransformedRange(TIterator, TIterator, Function)
    -> TransformedRange<TIterator,
                        std::invoke_result_t<Function, IteratorValue>>;

#endif  // INCLUDED_SRC_OTHER_TOOLS_TRANSFORMED_RANGE_HPP
