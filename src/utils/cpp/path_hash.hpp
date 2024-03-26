// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_UTILS_CPP_PATH_HASH_HPP
#define INCLUDED_SRC_UTILS_CPP_PATH_HASH_HPP

#include <cstddef>
#include <filesystem>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define GLIBCXX_11_4 20230528  // gcc/DATESTAMP of version 11.4
#if (defined(__GLIBCXX__) and _GLIBCXX_RELEASE < 12 and            \
     !(_GLIBCXX_RELEASE == 11 and __GLIBCXX__ >= GLIBCXX_11_4)) or \
    (defined(_LIBCPP_VERSION) and _LIBCPP_VERSION < 16000)
// std::hash<std::filesystem::path> is missing for
// - GNU's libstdc++ < 11.4
// - LLVM's libcxx < 16 (see https://reviews.llvm.org/D125394)
namespace std {
template <>
struct hash<std::filesystem::path> {
    [[nodiscard]] auto operator()(
        std::filesystem::path const& ct) const noexcept -> std::size_t {
        return std::filesystem::hash_value(ct);
    }
};
}  // namespace std
#endif

#endif  // INCLUDED_SRC_UTILS_CPP_PATH_HASH_HPP
