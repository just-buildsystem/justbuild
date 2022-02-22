#include <iostream>
#include "greet.hpp"

// this is a modification that has no effect on the produced binary

void greet(std::string const& name) {
    std::cout << "Hello " << name << std::endl;
}
