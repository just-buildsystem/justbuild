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
#include <cstddef>
#include <string>
#include <type_traits>

// TODO(modernize): remove this once std::derived_from is shipped with libcxx
template <class T, class U>
concept derived_from =
    std::is_base_of_v<U, T> &&
    std::is_convertible_v<const volatile T*, const volatile U*>;

// TODO(modernize): remove this once std::same_as is shipped with libcxx
template <class T, class U>
concept same_as = std::is_same_v<T, U> and std::is_same_v<U, T>;

template <class T>
concept ContainsString = requires { typename T::value_type; } and
                         std::is_same_v<typename T::value_type, std::string>;

template <class T>
concept HasSize = requires(T const c) {
    {
        c.size()
    } -> same_as<std::size_t>;  // TODO(modernize): replace by std::same_as
};

template <class T>
concept InputIterableContainer = requires(T const c) {
    {
        c.begin()
    } -> same_as<typename T::const_iterator>;  // TODO(modernize): replace by
                                               // std::input_iterator
    {
        c.end()
    } -> same_as<typename T::const_iterator>;  // TODO(modernize): replace by
                                               // std::input_iterator
};

template <class T>
concept OutputIterableContainer = InputIterableContainer<T> and requires(T c) {
    {
        std::inserter(c, c.begin())
    } -> same_as<std::insert_iterator<T>>;  // TODO(modernize): replace by
                                            // std::output_iterator
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
    } -> same_as<std::string const>;
};

#endif  // INCLUDED_SRC_UTILS_CPP_CONCEPTS_HPP
