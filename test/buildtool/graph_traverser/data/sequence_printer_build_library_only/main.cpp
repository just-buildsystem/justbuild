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

#include <iostream>

#include "fibonacci.hpp"
#include "printer.hpp"
#include "random_dna_sequence.hpp"

int main() {
    Printer printer;

    Fibonacci fib;
    std::cout
        << "PRINT 10 following terms of Fibonacci sequence starting with 0 1"
        << std::endl;
    printer.print(fib, 10U);
    std::cout << std::endl;

    Fibonacci fib2_5{2, 5};
    std::cout
        << "PRINT 8 following terms of Fibonacci sequence starting with 2 5"
        << std::endl;
    printer.print(fib2_5, 8U);
    std::cout << std::endl;

    RandomDNASequence piece_of_something;
    std::cout << "PRINT a random dna sequence of length 3" << std::endl;
    printer.print(piece_of_something, 30U);
}
