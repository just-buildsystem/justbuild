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

#ifndef INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP
#define INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP

#include <chrono>
#include <concepts>
#include <cstddef>
#include <ctime>
#include <iterator>
#include <string>
#include <type_traits>

template <class T>
concept ContainsString = requires { typename T::value_type; } and
                         std::same_as<typename T::value_type, std::string>;

template <class T>
concept HasSize =
    requires(T const c) { std::same_as<decltype(c.size()), std::size_t>; };

template <class T>
concept InputIterableContainer = requires(T const c) {
    std::input_iterator<decltype(c.begin())>;
    std::input_iterator<decltype(c.end())>;
};

template <class T>
concept OutputIterableContainer = requires(T c) {
    InputIterableContainer<T>;
    std::output_iterator<decltype(c.begin()), typename T::value_type>;
};

template <class T>
concept InputIterableStringContainer =
    InputIterableContainer<T> and ContainsString<T>;

// TODO(modernize): remove this once we require clang version >= 14.0.0
template <typename T>
concept ClockHasFromSys =
    requires(std::chrono::time_point<std::chrono::system_clock> const tp) {
        T::from_sys(tp);
    };

// TODO(modernize): remove this once we require clang version >= 14.0.0
template <typename T>
concept ClockHasFromTime = requires(std::time_t const t) { T::from_time_t(t); };

template <typename T>
concept StrMapConstForwardIterator = requires(T const c) {
    {
        std::remove_reference_t<decltype((*c).first)>{(*c).first}
    } -> std::same_as<std::string const>;
};

#endif  // INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP
