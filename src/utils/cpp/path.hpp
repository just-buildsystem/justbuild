#ifndef INCLUDED_SRC_UTILS_CPP_PATH_HPP
#define INCLUDED_SRC_UTILS_CPP_PATH_HPP

#include <filesystem>

[[nodiscard]] static inline auto ToNormalPath(std::filesystem::path const& p)
    -> std::filesystem::path {
    auto n = p.lexically_normal();
    if (not n.has_filename()) {
        n = n.parent_path();
    }
    if (n.empty()) {
        return std::filesystem::path{"."};
    }
    return n;
}

#endif
