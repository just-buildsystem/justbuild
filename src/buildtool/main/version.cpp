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

#include "src/buildtool/main/version.hpp"

#include <cstddef>

#include "nlohmann/json.hpp"
#include "src/utils/cpp/json.hpp"

auto version() -> std::string {
    std::size_t major = 1;
    std::size_t minor = 4;
    std::size_t revision = 0;
    std::string suffix = std::string{};
#ifdef VERSION_EXTRA_SUFFIX
    suffix += VERSION_EXTRA_SUFFIX;
#endif

    nlohmann::json version_info = {{"version", {major, minor, revision}},
                                   {"suffix", suffix}};

#ifdef SOURCE_DATE_EPOCH
    version_info["SOURCE_DATE_EPOCH"] = (std::size_t)SOURCE_DATE_EPOCH;
#else
    version_info["SOURCE_DATE_EPOCH"] = nullptr;
#endif

    return IndentOnlyUntilDepth(version_info, 2, 1, {});
}
