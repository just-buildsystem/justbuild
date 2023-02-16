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

#include <string>

[[nodiscard]] static inline auto QuoteForShell(std::string const& input)
    -> std::string {
    // To correctly quote a string for shell, replace ever ~'~ char with ~'\''~
    // and then put the resulting inside a pair of ~'~s.
    auto output = std::string(R"(')");
    auto start = input.begin();
    auto pos = input.find('\'');
    while (pos != std::string::npos) {
        output.append(
            start, start + static_cast<std::string::difference_type>(pos + 1));
        output += R"(\'')";
        start += static_cast<std::string::difference_type>(pos + 1);
        pos = input.find('\'', pos + 1);
    }
    output.append(start, input.end());
    output += R"(')";
    return output;
}
