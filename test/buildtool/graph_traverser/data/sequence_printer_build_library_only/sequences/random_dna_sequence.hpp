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
