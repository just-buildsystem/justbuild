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

#ifndef INCLUDED_SRC_UTILS_CPP_HASH_COMBINE_HPP
#define INCLUDED_SRC_UTILS_CPP_HASH_COMBINE_HPP

#include <cstddef>

#include "gsl/gsl"

// Taken from Boost, as hash_combine did not yet make it to STL.
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0814r0.pdf
template <class T>
inline auto hash_combine(gsl::not_null<std::size_t*> const& seed,
                         T const& v) -> void {
    *seed ^=
        std::hash<T>{}(v) + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);  // NOLINT
}

#endif
