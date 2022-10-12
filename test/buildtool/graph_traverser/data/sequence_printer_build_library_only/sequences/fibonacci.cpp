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

#include "fibonacci.hpp"

Fibonacci::Fibonacci() : second_prev_{0}, prev_{1} {}

Fibonacci::Fibonacci(int zero_th, int first)
    : second_prev_{zero_th}, prev_{first} {}

int Fibonacci::next() {
    int next = second_prev_ + prev_;
    second_prev_ = prev_;
    prev_ = next;
    return next;
}

std::string Fibonacci::separator() {
    return ", ";
}
