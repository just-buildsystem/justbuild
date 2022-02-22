#pragma once

#include <string>

template <typename T>
class Sequence {
  public:
    typedef T value_type;

    virtual T next() = 0;

    virtual std::string separator() = 0;

    virtual ~Sequence() = default;
};
