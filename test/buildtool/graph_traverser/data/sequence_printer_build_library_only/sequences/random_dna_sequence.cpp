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

#include "random_dna_sequence.hpp"

RandomDNASequence::RandomDNASequence()
    : eng_(static_cast<unsigned int>(
          std::chrono::system_clock::now().time_since_epoch().count())),
      dist_(0, 3) {}

RandomDNASequence::RandomDNASequence(unsigned int seed)
    : eng_(seed), dist_(0, 3) {}

char RandomDNASequence::next() {
    int option = dist_(eng_);
    if (option == 0)
        return 'A';
    if (option == 1)
        return 'C';
    if (option == 2)
        return 'G';

    return 'T';
}

std::string RandomDNASequence::separator() {
    return "";
}
