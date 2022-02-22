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
