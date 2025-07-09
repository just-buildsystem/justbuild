// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/common/remote/remote_common.hpp"

#include <optional>
#include <string>
#include <utility>

#include "catch2/catch_test_macros.hpp"

TEST_CASE("Test parsing execution properties", "[remote_config]") {
    SECTION("execution property has colon in value") {
        std::optional<std::pair<std::string, std::string>> parsed_result =
            ParseProperty("image:test.registry:443/test");
        CHECK(parsed_result != std::nullopt);
        CHECK(parsed_result.value().first == "image");
        CHECK(parsed_result.value().second == "test.registry:443/test");
    }
    SECTION("execution property has empty value") {
        std::optional<std::pair<std::string, std::string>> parsed_result =
            ParseProperty("image:");
        CHECK(parsed_result == std::nullopt);
    }
    SECTION("execution property has no colon") {
        std::optional<std::pair<std::string, std::string>> parsed_result =
            ParseProperty("image");
        CHECK(parsed_result == std::nullopt);
    }
    SECTION("execution property has key:val format") {
        std::optional<std::pair<std::string, std::string>> parsed_result =
            ParseProperty("image:test");
        CHECK(parsed_result != std::nullopt);
        CHECK(parsed_result.value().first == "image");
        CHECK(parsed_result.value().second == "test");
    }
}
