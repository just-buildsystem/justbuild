// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "test/utils/large_objects/large_object_utils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/file_system/file_system_manager.hpp"

namespace {
class Randomizer final {
  public:
    Randomizer(std::uint64_t min, std::uint64_t max) noexcept
        : range_(std::random_device{}()), distribution_(min, max) {}

    [[nodiscard]] auto Get() noexcept -> std::uint64_t {
        return distribution_(range_);
    }

  private:
    std::mt19937_64 range_;
    std::uniform_int_distribution<std::mt19937_64::result_type> distribution_;
};

/// \brief Create a number of chunks of the predefined size.
/// \tparam UChunkLength    Length of each chunk.
/// \tparam USize           Number of chunks.
template <std::size_t kChunkLength, std::size_t kPoolSize>
class ChunkPool final {
  public:
    [[nodiscard]] static auto Instance() noexcept
        -> ChunkPool<kChunkLength, kPoolSize> const& {
        static ChunkPool<kChunkLength, kPoolSize> pool;
        return pool;
    }

    [[nodiscard]] auto operator[](std::size_t index) const noexcept
        -> std::string const& {
        return gsl::at(pool_, gsl::narrow<gsl::index>(index));
    }

  private:
    std::array<std::string, kPoolSize> pool_;

    explicit ChunkPool() noexcept {
        // Starts from 1 to exclude '\0' from randomization
        Randomizer randomizer{1, std::numeric_limits<char>::max()};

        for (std::size_t i = 0; i < pool_.size(); ++i) {
            auto& chunk = gsl::at(pool_, gsl::narrow<gsl::index>(i));
            chunk.resize(kChunkLength);
            for (std::size_t j = 0; j < kChunkLength; ++j) {
                chunk[j] = randomizer.Get();
            }
        }
    }
};
}  // namespace

auto LargeObjectUtils::GenerateFile(std::filesystem::path const& path,
                                    std::uintmax_t size,
                                    bool is_executable) noexcept -> bool {
    // Remove the file, if exists:
    if (not FileSystemManager::RemoveFile(path)) {
        return false;
    }

    static constexpr std::size_t kChunkLength = 128;
    static constexpr std::size_t kPoolSize = 64;
    using Pool = ChunkPool<kChunkLength, kPoolSize>;

    // To create a random file, the initial chunk position and the shift are
    // randomized:
    Randomizer randomizer{std::numeric_limits<std::size_t>::min(),
                          std::numeric_limits<std::size_t>::max()};
    const std::size_t pool_index = randomizer.Get() % kPoolSize;
    const std::size_t pool_shift = randomizer.Get() % 10;
    const std::size_t step_count = size / kChunkLength + 1;

    try {
        std::ofstream stream(path);
        for (std::size_t i = 0; i < step_count and stream.good(); ++i) {
            const std::size_t index = (pool_index + i * pool_shift) % kPoolSize;
            if (i != step_count - 1) {
                stream << Pool::Instance()[index];
            }
            else {
                auto count = std::min(size - kChunkLength * i, kChunkLength);
                stream << Pool::Instance()[index].substr(0, count);
            }
        }
        if (not stream.good()) {
            return false;
        }
        stream.close();
    } catch (...) {
        return false;
    }

    if (is_executable) {
        using perms = std::filesystem::perms;
        perms p = perms::owner_exec | perms::group_exec | perms::others_exec;
        try {
            std::filesystem::permissions(
                path, p, std::filesystem::perm_options::add);
        } catch (...) {
            return false;
        }
    }
    return true;
}

auto LargeObjectUtils::GenerateDirectory(std::filesystem::path const& path,
                                         std::uintmax_t entries_count) noexcept
    -> bool {
    // Recreate the directory:
    if (not FileSystemManager::RemoveDirectory(path) or
        not FileSystemManager::CreateDirectory(path)) {
        return false;
    }

    Randomizer randomizer{std::numeric_limits<std::size_t>::min(),
                          std::numeric_limits<std::size_t>::max()};

    std::uintmax_t entries = 0;
    while (entries < entries_count) {
        // Randomize the number for a file:
        auto const random_number = randomizer.Get();
        auto const file_name =
            std::string(kTreeEntryPrefix) + std::to_string(random_number);
        std::filesystem::path const file_path = path / file_name;

        // Check file uniqueness:
        if (FileSystemManager::IsFile(file_path)) {
            continue;
        }

        try {
            std::ofstream stream(file_path);
            stream << random_number;
            stream.close();
        } catch (...) {
            return false;
        }
        ++entries;
    }
    return true;
}
