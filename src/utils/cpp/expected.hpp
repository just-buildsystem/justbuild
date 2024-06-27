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

#ifndef INCLUDED_SRC_UTILS_CPP_EXPECTED_HPP
#define INCLUDED_SRC_UTILS_CPP_EXPECTED_HPP

#include <utility>
#include <variant>

// TODO(modernize): replace this by std::unexpected once we switched to C++23
template <class E>
class unexpected {
  public:
    explicit unexpected(E error) : error_{std::move(error)} {}
    [[nodiscard]] auto error() && -> E { return std::move(error_); }

  private:
    E error_;
};

// TODO(modernize): replace this by std::expected once we switched to C++23
template <class T, class E>
class expected {
  public:
    expected(T value) noexcept  // NOLINT
        : value_{std::in_place_index<0>, std::move(value)} {}
    expected(unexpected<E> unexpected) noexcept  // NOLINT
        : value_{std::in_place_index<1>, std::move(unexpected).error()} {}

    // Check whether the object contains an expected value
    [[nodiscard]] auto has_value() const noexcept -> bool {
        return value_.index() == 0;
    }
    [[nodiscard]] operator bool() const noexcept {  // NOLINT
        return has_value();
    }

    // Return the expected value
    [[nodiscard]] auto value() const& -> T const& {
        return std::get<0>(value_);
    }
    [[nodiscard]] auto value() && -> T {
        return std::move(std::get<0>(value_));
    }

    // Access the expected value
    [[nodiscard]] auto operator*() const& noexcept -> T const& {
        return *std::get_if<0>(&value_);
    }
    [[nodiscard]] auto operator*() && noexcept -> T {
        return std::move(*std::get_if<0>(&value_));
    }
    [[nodiscard]] auto operator->() const noexcept -> T const* {
        return std::get_if<0>(&value_);
    }

    // Access the unexpected value
    [[nodiscard]] auto error() const& noexcept -> const E& {
        return *std::get_if<1>(&value_);
    }
    [[nodiscard]] auto error() && noexcept -> E {
        return std::move(*std::get_if<1>(&value_));
    }

  private:
    std::variant<T, E> value_;
};

#endif  // INCLUDED_SRC_UTILS_CPP_EXPECTED_HPP
