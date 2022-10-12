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

#ifndef INCLUDED_SRC_UTILS_CPP_VECTOR_HPP
#define INCLUDED_SRC_UTILS_CPP_VECTOR_HPP

// small library to manipulate vectors
#include <vector>

// sort the passed vector and remove repeated entries
template <typename T>
void sort_and_deduplicate(std::vector<T>* x) {
    std::sort(x->begin(), x->end());
    auto it = std::unique(x->begin(), x->end());
    x->erase(it, x->end());
}
#endif
