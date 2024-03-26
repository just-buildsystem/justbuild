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

#include "src/buildtool/crypto/hash_function.hpp"

#include <cstddef>
#include <fstream>
#include <string>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

[[nodiscard]] auto HashFunction::ComputeHashFile(
    const std::filesystem::path& file_path,
    bool as_tree) noexcept
    -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>> {
    static constexpr std::size_t kChunkSize{4048};
    try {
        auto hasher = Hasher();
        auto size = std::filesystem::file_size(file_path);
        if (HashType() == JustHash::Native) {
            hasher.Update(
                (as_tree ? std::string{"tree "} : std::string{"blob "}) +
                std::to_string(size) + '\0');
        }
        std::string chunk{};
        chunk.resize(kChunkSize);
        std::ifstream file_reader(file_path.string(), std::ios::binary);
        if (file_reader.is_open()) {
            auto chunk_size = gsl::narrow<std::streamsize>(chunk.size());
            do {
                file_reader.read(chunk.data(), chunk_size);
                auto count = file_reader.gcount();
                hasher.Update(chunk.substr(0, gsl::narrow<std::size_t>(count)));
            } while (file_reader.good());
            file_reader.close();
            return std::make_pair(std::move(hasher).Finalize(), size);
        }
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Debug,
                    "Error while trying to hash {}: {}",
                    file_path.string(),
                    e.what());
    }
    return std::nullopt;
}
