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

#include "src/utils/cpp/incremental_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/tmp_dir.hpp"
#include "test/utils/hermeticity/test_storage_config.hpp"
#include "test/utils/large_objects/large_object_utils.hpp"

[[nodiscard]] static auto ReadFile(std::filesystem::path const& path) noexcept
    -> std::optional<std::string> {
    try {
        std::size_t const file_size = std::filesystem::file_size(path);
        std::string result(file_size, '\0');

        std::ifstream istream(path.string(), std::ios_base::binary);
        if (not istream.good()) {
            return std::nullopt;
        }
        istream.read(result.data(), static_cast<std::streamsize>(file_size));
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

TEST_CASE("IncrementalReader", "[incremental_reader]") {
    static constexpr auto kFileSize =
        static_cast<std::uintmax_t>(5 * 1024 * 1024);

    static constexpr std::size_t kChunkWithRemainder = 107;
    static_assert(kFileSize % kChunkWithRemainder != 0);

    static constexpr std::size_t kChunkWithoutRemainder = 128;
    static_assert(kFileSize % kChunkWithoutRemainder == 0);

    auto const config = TestStorageConfig::Create();

    auto temp_dir = config.Get().CreateTypedTmpDir("incremental_reader");
    REQUIRE(temp_dir);

    auto const file_path = temp_dir->GetPath() / "file";
    REQUIRE(LargeObjectUtils::GenerateFile(file_path, kFileSize));

    auto const file_content = ::ReadFile(file_path);
    REQUIRE(file_content.has_value());

    SECTION("File") {
        auto reader =
            IncrementalReader::FromFile(kChunkWithRemainder, file_path);
        REQUIRE(reader.has_value());

        std::string result;
        result.reserve(reader->GetContentSize());
        for (auto chunk : *reader) {
            REQUIRE(chunk.has_value());
            result.append(*chunk);
        }
        REQUIRE(result.size() == file_content->size());
        REQUIRE(result == *file_content);

        reader = IncrementalReader::FromFile(kChunkWithoutRemainder, file_path);
        REQUIRE(reader.has_value());

        result.clear();
        for (auto chunk : *reader) {
            REQUIRE(chunk.has_value());
            result.append(*chunk);
        }
        REQUIRE(result.size() == file_content->size());
        REQUIRE(result == *file_content);
    }
}

TEST_CASE("IncrementalReader - Empty", "[incremental_reader]") {
    static constexpr std::size_t kChunkSize = 128;
    SECTION("File") {
        auto const config = TestStorageConfig::Create();

        auto temp_dir = config.Get().CreateTypedTmpDir("incremental_reader");
        REQUIRE(temp_dir);

        std::filesystem::path const empty_file = temp_dir->GetPath() / "file";
        {
            std::ofstream stream(empty_file);
            stream << "";
        }
        auto const reader = IncrementalReader::FromFile(kChunkSize, empty_file);
        REQUIRE(reader.has_value());

        std::optional<std::string> result;
        for (auto chunk : *reader) {
            REQUIRE(chunk.has_value());
            if (not result.has_value()) {
                result = std::string{};
            }
            result->append(*chunk);
        }
        REQUIRE(result.has_value());
        REQUIRE(result->empty());
    }
}
