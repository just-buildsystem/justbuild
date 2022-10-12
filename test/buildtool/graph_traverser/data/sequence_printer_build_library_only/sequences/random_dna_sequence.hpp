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

#pragma once

#include <chrono>
#include <random>
#include <string>
#include "sequence.hpp"

class RandomDNASequence : public Sequence<char> {
  public:
    RandomDNASequence();
    explicit RandomDNASequence(unsigned int seed);
    ~RandomDNASequence() override = default;
    char next() override;
    std::string separator() override;

  private:
    std::default_random_engine eng_;
    std::uniform_int_distribution<> dist_;
};
