#pragma once

#include <string>
#include "sequence.hpp"

class Fibonacci : public Sequence<int> {
  public:
    Fibonacci();
    Fibonacci(int zeroth, int first);
    ~Fibonacci() override = default;
    int next() override;
    std::string separator() override;

  private:
    int second_prev_;
    int prev_;
};
