#ifndef INCLUDED_SRC_UTILS_CPP_VECTOR_HPP
#define INCLUDED_SRC_UTILS_CPP_VECTOR_HPP

// small library to manipulate vectors
#include <vector>

// sort the passed vector and remove repeated entries
template <typename T>
void sort_and_deduplicate(std::vector<T>* x) {
    std::sort(x->begin(), x->end());
    auto it = std::unique(x->begin(), x->end());
    x->erase(it, x->end());
}
#endif