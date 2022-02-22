#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_COMMON_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_COMMON_HPP

/// \file bazel_common.hpp
/// \brief Common types and functions required by Bazel API.

#include <cstdint>

#include "src/utils/cpp/type_safe_arithmetic.hpp"

// Port
struct PortTag : type_safe_arithmetic_tag<std::uint16_t> {};
using Port = type_safe_arithmetic<PortTag>;

struct ExecutionConfiguration {
    int execution_priority{};
    int results_cache_priority{};
    bool skip_cache_lookup{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_COMMON_HPP
