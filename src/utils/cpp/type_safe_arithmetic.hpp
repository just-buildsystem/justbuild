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

/// \struct TypeSafeArithmeticTag
/// \brief Abstract tag defining types and limits for custom arithmetic types.
/// Usage example:
/// struct my_type_tag : TypeSafeArithmeticTag<int, -2, +3> {};
/// using my_type_t = TypeSafeArithmetic<my_type_tag>;
template <typename T,
          T kMin = std::numeric_limits<T>::lowest(),
          T kMax = std::numeric_limits<T>::max(),
          T kSmallest = std::numeric_limits<T>::min()>
struct TypeSafeArithmeticTag {
    static_assert(std::is_arithmetic_v<T>,
                  "T must be an arithmetic type (integer or floating-point)");

    using value_t = T;
    using reference_t = T&;
    using const_reference_t = T const&;
    using pointer_t = T*;
    using const_pointer_t = T const*;

    static constexpr value_t kMaxValue = kMax;
    static constexpr value_t kMinValue = kMin;
    static constexpr value_t kSmallestValue = kSmallest;
};

/// \class TypeSafeArithmetic
/// \brief Abstract class for defining custom arithmetic types.
/// \tparam TAG The actual \ref TypeSafeArithmeticTag
template <typename TAG>
class TypeSafeArithmetic {
    typename TAG::value_t value_{};

  public:
    using tag_t = TAG;
    using value_t = typename tag_t::value_t;
    using reference_t = typename tag_t::reference_t;
    using const_reference_t = typename tag_t::const_reference_t;
    using pointer_t = typename tag_t::pointer_t;
    using const_pointer_t = typename tag_t::const_pointer_t;

    static constexpr value_t kMaxValue = tag_t::kMaxValue;
    static constexpr value_t kMinValue = tag_t::kMinValue;
    static constexpr value_t kSmallestValue = tag_t::kSmallestValue;

    constexpr TypeSafeArithmetic() = default;

    // NOLINTNEXTLINE
    constexpr /*explicit*/ TypeSafeArithmetic(value_t value) { set(value); }

    TypeSafeArithmetic(TypeSafeArithmetic const&) = default;
    TypeSafeArithmetic(TypeSafeArithmetic&&) noexcept = default;
    auto operator=(TypeSafeArithmetic const&) -> TypeSafeArithmetic& = default;
    auto operator=(TypeSafeArithmetic&&) noexcept -> TypeSafeArithmetic& =
                                                         default;
    ~TypeSafeArithmetic() = default;

    auto operator=(value_t value) -> TypeSafeArithmetic& {
        set(value);
        return *this;
    }

    // NOLINTNEXTLINE
    constexpr /*explicit*/ operator value_t() const { return value_; }

    constexpr auto get() const -> value_t { return value_; }

    constexpr void set(value_t value) {
        Expects(value >= kMinValue and value <= kMaxValue and
                "value output of range");
        value_ = value;
    }

    auto pointer() const -> const_pointer_t { return &value_; }
};

// template <typename TAG>
// bool operator==(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return lhs.get() == rhs.get();
// }
//
// template <typename TAG>
// bool operator!=(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return !(lhs == rhs);
// }
//
// template <typename TAG>
// bool operator>(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return lhs.get() > rhs.get();
// }
//
// template <typename TAG>
// bool operator>=(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return lhs.get() >= rhs.get();
// }
//
// template <typename TAG>
// bool operator<(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return lhs.get() < rhs.get();
// }
//
// template <typename TAG>
// bool operator<=(TypeSafeArithmetic<TAG> lhs, TypeSafeArithmetic<TAG> rhs)
// {
//     return lhs.get() <= rhs.get();
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG> operator+(TypeSafeArithmetic<TAG> lhs,
//                                     TypeSafeArithmetic<TAG> rhs) {
//     return TypeSafeArithmetic<TAG>{lhs.get() + rhs.get()};
// }

template <typename TAG>
auto operator+=(TypeSafeArithmetic<TAG>& lhs,
                TypeSafeArithmetic<TAG> rhs) -> TypeSafeArithmetic<TAG>& {
    lhs.set(lhs.get() + rhs.get());
    return lhs;
}

// template <typename TAG>
// TypeSafeArithmetic<TAG> operator-(TypeSafeArithmetic<TAG> lhs,
//                                     TypeSafeArithmetic<TAG> rhs) {
//     return TypeSafeArithmetic<TAG>{lhs.get() - rhs.get()};
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG>& operator-=(TypeSafeArithmetic<TAG>& lhs,
//                                       TypeSafeArithmetic<TAG> rhs) {
//     lhs.set(lhs.get() - rhs.get());
//     return lhs;
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG> operator*(TypeSafeArithmetic<TAG> lhs,
//                                     typename TAG::value_t rhs) {
//     return TypeSafeArithmetic<TAG>{lhs.get() - rhs};
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG>& operator*=(TypeSafeArithmetic<TAG>& lhs,
//                                       typename TAG::value_t rhs) {
//     lhs.set(lhs.get() * rhs);
//     return lhs;
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG> operator/(TypeSafeArithmetic<TAG> lhs,
//                                     typename TAG::value_t rhs) {
//     return TypeSafeArithmetic<TAG>{lhs.get() / rhs};
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG>& operator/=(TypeSafeArithmetic<TAG>& lhs,
//                                       typename TAG::value_t rhs) {
//     lhs.set(lhs.get() / rhs);
//     return lhs;
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG>& operator++(TypeSafeArithmetic<TAG>& a) {
//     return a += TypeSafeArithmetic<TAG>{1};
// }

template <typename TAG>
auto operator++(TypeSafeArithmetic<TAG>& a, int) -> TypeSafeArithmetic<TAG> {
    auto r = a;
    a += TypeSafeArithmetic<TAG>{1};
    return r;
}

// template <typename TAG>
// TypeSafeArithmetic<TAG>& operator--(TypeSafeArithmetic<TAG>& a) {
//     return a -= TypeSafeArithmetic<TAG>{1};
// }
//
// template <typename TAG>
// TypeSafeArithmetic<TAG> operator--(TypeSafeArithmetic<TAG>& a, int) {
//     auto r = a;
//     a += TypeSafeArithmetic<TAG>{1};
//     return r;
// }

#endif  // INCLUDED_SRC_UTILS_CPP_TYPE_SAFE_ARITHMETIC_HPP
