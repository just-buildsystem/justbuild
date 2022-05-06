#ifndef INCLUDED_SRC_UTILS_CPP_PATH_HPP
#define INCLUDED_SRC_UTILS_CPP_PATH_HPP

#include <filesystem>

[[nodiscard]] static inline auto ToNormalPath(std::filesystem::path const& p)
    -> std::filesystem::path {
    auto n = p.lexically_normal();
    if (not n.has_filename()) {
        return n.parent_path();
    }
    return n;
}

#endif
