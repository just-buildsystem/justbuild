// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_PORT_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_PORT_HPP

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/type_safe_arithmetic.hpp"

// Port
struct PortTag : TypeSafeArithmeticTag<std::uint16_t> {};
using Port = TypeSafeArithmetic<PortTag>;

[[nodiscard]] static auto ParsePort(int const port_num) noexcept
    -> std::optional<Port> {
    try {
        static constexpr int kMaxPortNumber{
            std::numeric_limits<std::uint16_t>::max()};
        if (port_num >= 0 and port_num <= kMaxPortNumber) {
            return gsl::narrow_cast<Port::value_t>(port_num);
        }
    } catch (std::out_of_range const& e) {
        Logger::Log(LogLevel::Error, "Port raised out_of_range exception.");
    }
    return std::nullopt;
}

[[nodiscard]] static auto ParsePort(std::string const& port) noexcept
    -> std::optional<Port> {
    try {
        auto port_num = std::stoi(port);
        return ParsePort(port_num);
    } catch (std::invalid_argument const& e) {
        Logger::Log(LogLevel::Error, "Port raised invalid_argument exception.");
    }
    return std::nullopt;
}

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_PORT_HPP
