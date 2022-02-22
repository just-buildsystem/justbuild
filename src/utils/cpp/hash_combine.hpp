#ifndef INCLUDED_SRC_UTILS_CPP_HASH_COMBINE_HPP
#define INCLUDED_SRC_UTILS_CPP_HASH_COMBINE_HPP

#include "gsl-lite/gsl-lite.hpp"

// Taken from Boost, as hash_combine did not yet make it to STL.
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0814r0.pdf
template <class T>
inline auto hash_combine(gsl::not_null<std::size_t*> const& seed, T const& v)
    -> void {
    *seed ^=
        std::hash<T>{}(v) + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);  // NOLINT
}

#endif
