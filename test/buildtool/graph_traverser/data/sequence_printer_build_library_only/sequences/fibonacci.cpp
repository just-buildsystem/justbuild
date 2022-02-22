#include "fibonacci.hpp"

Fibonacci::Fibonacci() : second_prev_{0}, prev_{1} {}

Fibonacci::Fibonacci(int zero_th, int first)
    : second_prev_{zero_th}, prev_{first} {}

int Fibonacci::next() {
    int next = second_prev_ + prev_;
    second_prev_ = prev_;
    prev_ = next;
    return next;
}

std::string Fibonacci::separator() {
    return ", ";
}
