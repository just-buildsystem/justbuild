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
