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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_OUTPUTSCHECK_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_OUTPUTSCHECK_HPP

#ifndef BOOTSTRAP_BUILD_TOOL

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/buildtool/common/bazel_types.hpp"

[[nodiscard]] static inline auto ActionResultContainsExpectedOutputs(
    const bazel_re::ActionResult& result,
    const std::vector<std::string>& expected_files,
    const std::vector<std::string>& expected_dirs) noexcept -> bool {
    try {
        std::set<std::string> actual_output_files{};
        for (auto const& file : result.output_files()) {
            actual_output_files.emplace(file.path());
        }
        for (auto const& file : result.output_file_symlinks()) {
            actual_output_files.emplace(file.path());
        }
        for (auto const& file : result.output_directory_symlinks()) {
            actual_output_files.emplace(file.path());
        }
        if (not std::all_of(expected_files.begin(),
                            expected_files.end(),
                            [&actual_output_files](auto const& expected) {
                                return actual_output_files.contains(expected);
                            })) {
            return false;
        }

        std::set<std::string> actual_output_dirs{};
        for (auto const& dir : result.output_directories()) {
            actual_output_dirs.emplace(dir.path());
        }
        return std::all_of(expected_dirs.begin(),
                           expected_dirs.end(),
                           [&actual_output_dirs](auto const& expected) {
                               return actual_output_dirs.contains(expected);
                           });
    } catch (...) {
        return false;
    }
}

#endif

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_UTILS_OUTPUTSCHECK_HPP
