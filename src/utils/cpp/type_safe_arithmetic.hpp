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

#ifndef INCLUDED_SRC_UTILS_CPP_TYPE_SAFE_ARITHMETIC_HPP
#define INCLUDED_SRC_UTILS_CPP_TYPE_SAFE_ARITHMETIC_HPP

#include <limits>
#include <type_traits>

#include "gsl/gsl"

/// \struct type_safe_arithmetic_tag
/// \brief Abstract tag defining types and limits for custom arithmetic types.
/// Usage example:
/// struct my_type_tag : type_safe_arithmetic_tag<int, -2, +3> {};
/// using my_type_t = type_safe_arithmetic<my_type_tag>;
template <typename T,
          T MIN_VALUE = std::numeric_limits<T>::lowest(),
          T MAX_VALUE = std::numeric_limits<T>::max(),
          T SMALLEST_VALUE = std::numeric_limits<T>::min()>
struct type_safe_arithmetic_tag {
    static_assert(std::is_arithmetic<T>::value,
                  "T must be an arithmetic type (integer or floating-point)");

    using value_t = T;
    using reference_t = T&;
    using const_reference_t = T const&;
    using pointer_t = T*;
    using const_pointer_t = T const*;

    static constexpr value_t max_value = MAX_VALUE;
    static constexpr value_t min_value = MIN_VALUE;
    static constexpr value_t smallest_value = SMALLEST_VALUE;
};

/// \class type_safe_arithmetic
/// \brief Abstract class for defining custom arithmetic types.
/// \tparam TAG The actual \ref type_safe_arithmetic_tag
template <typename TAG>
class type_safe_arithmetic {
    typename TAG::value_t m_value{};

  public:
    using tag_t = TAG;
    using value_t = typename tag_t::value_t;
    using reference_t = typename tag_t::reference_t;
    using const_reference_t = typename tag_t::const_reference_t;
    using pointer_t = typename tag_t::pointer_t;
    using const_pointer_t = typename tag_t::const_pointer_t;

    static constexpr value_t max_value = tag_t::max_value;
    static constexpr value_t min_value = tag_t::min_value;
    static constexpr value_t smallest_value = tag_t::smallest_value;

    constexpr type_safe_arithmetic() = default;

    // NOLINTNEXTLINE
    constexpr /*explicit*/ type_safe_arithmetic(value_t value) { set(value); }

    type_safe_arithmetic(type_safe_arithmetic const&) = default;
    type_safe_arithmetic(type_safe_arithmetic&&) noexcept = default;
    auto operator=(type_safe_arithmetic const&) -> type_safe_arithmetic& =
                                                       default;
    auto operator=(type_safe_arithmetic&&) noexcept -> type_safe_arithmetic& =
                                                           default;
    ~type_safe_arithmetic() = default;

    auto operator=(value_t value) -> type_safe_arithmetic& {
        set(value);
        return *this;
    }

    // NOLINTNEXTLINE
    constexpr /*explicit*/ operator value_t() const { return m_value; }

    constexpr auto get() const -> value_t { return m_value; }

    constexpr void set(value_t value) {
        Expects(value >= min_value and value <= max_value and
                "value output of range");
        m_value = value;
    }

    auto pointer() const -> const_pointer_t { return &m_value; }
};

// template <typename TAG>
// bool operator==(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return lhs.get() == rhs.get();
// }
//
// template <typename TAG>
// bool operator!=(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return !(lhs == rhs);
// }
//
// template <typename TAG>
// bool operator>(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return lhs.get() > rhs.get();
// }
//
// template <typename TAG>
// bool operator>=(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return lhs.get() >= rhs.get();
// }
//
// template <typename TAG>
// bool operator<(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return lhs.get() < rhs.get();
// }
//
// template <typename TAG>
// bool operator<=(type_safe_arithmetic<TAG> lhs, type_safe_arithmetic<TAG> rhs)
// {
//     return lhs.get() <= rhs.get();
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG> operator+(type_safe_arithmetic<TAG> lhs,
//                                     type_safe_arithmetic<TAG> rhs) {
//     return type_safe_arithmetic<TAG>{lhs.get() + rhs.get()};
// }

template <typename TAG>
auto operator+=(type_safe_arithmetic<TAG>& lhs,
                type_safe_arithmetic<TAG> rhs) -> type_safe_arithmetic<TAG>& {
    lhs.set(lhs.get() + rhs.get());
    return lhs;
}

// template <typename TAG>
// type_safe_arithmetic<TAG> operator-(type_safe_arithmetic<TAG> lhs,
//                                     type_safe_arithmetic<TAG> rhs) {
//     return type_safe_arithmetic<TAG>{lhs.get() - rhs.get()};
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG>& operator-=(type_safe_arithmetic<TAG>& lhs,
//                                       type_safe_arithmetic<TAG> rhs) {
//     lhs.set(lhs.get() - rhs.get());
//     return lhs;
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG> operator*(type_safe_arithmetic<TAG> lhs,
//                                     typename TAG::value_t rhs) {
//     return type_safe_arithmetic<TAG>{lhs.get() - rhs};
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG>& operator*=(type_safe_arithmetic<TAG>& lhs,
//                                       typename TAG::value_t rhs) {
//     lhs.set(lhs.get() * rhs);
//     return lhs;
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG> operator/(type_safe_arithmetic<TAG> lhs,
//                                     typename TAG::value_t rhs) {
//     return type_safe_arithmetic<TAG>{lhs.get() / rhs};
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG>& operator/=(type_safe_arithmetic<TAG>& lhs,
//                                       typename TAG::value_t rhs) {
//     lhs.set(lhs.get() / rhs);
//     return lhs;
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG>& operator++(type_safe_arithmetic<TAG>& a) {
//     return a += type_safe_arithmetic<TAG>{1};
// }

template <typename TAG>
auto operator++(type_safe_arithmetic<TAG>& a,
                int) -> type_safe_arithmetic<TAG> {
    auto r = a;
    a += type_safe_arithmetic<TAG>{1};
    return r;
}

// template <typename TAG>
// type_safe_arithmetic<TAG>& operator--(type_safe_arithmetic<TAG>& a) {
//     return a -= type_safe_arithmetic<TAG>{1};
// }
//
// template <typename TAG>
// type_safe_arithmetic<TAG> operator--(type_safe_arithmetic<TAG>& a, int) {
//     auto r = a;
//     a += type_safe_arithmetic<TAG>{1};
//     return r;
// }

#endif  // INCLUDED_SRC_UTILS_CPP_TYPE_SAFE_ARITHMETIC_HPP
