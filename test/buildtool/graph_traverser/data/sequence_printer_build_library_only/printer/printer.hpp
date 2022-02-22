#pragma once

#include <iostream>

class Printer {
  public:
    template <class SequenceT>
    void print(SequenceT& seq, unsigned int number_of_terms) {
        if (number_of_terms == 0) {
            std::cout << std::endl;
            return;
        }
        for (unsigned int i = 0; i < number_of_terms - 1; ++i) {
            std::cout << seq.next() << seq.separator();
        }
        std::cout << seq.next() << std::endl;
    }
};
