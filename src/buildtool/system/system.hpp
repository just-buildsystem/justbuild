// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_HPP
#define INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_HPP

#include <chrono>

#include "src/utils/cpp/concepts.hpp"

namespace System {

void ExitWithoutCleanup(int exit_code);

/// \brief Obtain POSIX epoch time for a given clock.
/// Clocks may have different epoch times. To obtain the POSIX epoch time
/// (1970-01-01 00:00:00 UTC) for a given clock, it must be converted.
template <class T_Clock>
static auto GetPosixEpoch() -> std::chrono::time_point<T_Clock> {
    // Since C++20, the system clock's default value is the POSIX epoch time.
    std::chrono::time_point<std::chrono::system_clock> sys_epoch{};
    if constexpr (std::is_same_v<T_Clock, std::chrono::system_clock>) {
        // No conversion necessary for the system clock.
        return sys_epoch;
    }
    else if constexpr (ClockHasFromSys<T_Clock>) {
        // The correct C++20 way to perform the time point conversion.
        return T_Clock::from_sys(sys_epoch);
    }
    else if constexpr (ClockHasFromTime<T_Clock>) {
        // Older releases of libcxx did not implement the standard conversion
        // function from_sys() for std::chrono::file_clock. Instead the
        // non-standard function file_clock::from_time_t() must be used. Since
        // libcxx 14.0.0, this has been fixed by:
        //  - https://reviews.llvm.org/D113027
        //  - https://reviews.llvm.org/D113430
        // TODO(modernize): remove this once we require clang version >= 14.0.0
        return T_Clock::from_time_t(
            std::chrono::system_clock::to_time_t(sys_epoch));
    }
    static_assert(std::is_same_v<T_Clock, std::chrono::system_clock> or
                      ClockHasFromSys<T_Clock> or ClockHasFromTime<T_Clock>,
                  "Time point conversion function unavailable.");
    return {};
}

}  // namespace System

#endif  // INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_HPP
