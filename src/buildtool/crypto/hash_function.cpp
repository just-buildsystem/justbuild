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
#include <exception>
#include <string_view>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/incremental_reader.hpp"

namespace {
[[nodiscard]] auto CreateGitTreeTag(std::size_t size) noexcept -> std::string {
    return std::string("tree ") + std::to_string(size) + '\0';
}
[[nodiscard]] auto CreateGitBlobTag(std::size_t size) noexcept -> std::string {
    return std::string("blob ") + std::to_string(size) + '\0';
}
}  // namespace

auto HashFunction::HashBlobData(std::string const& data) const noexcept
    -> Hasher::HashDigest {
    return HashTaggedLine(data, CreateGitBlobTag);
}

auto HashFunction::HashTreeData(std::string const& data) const noexcept
    -> Hasher::HashDigest {
    return HashTaggedLine(data, CreateGitTreeTag);
}

auto HashFunction::PlainHashData(std::string const& data) const noexcept
    -> Hasher::HashDigest {
    return HashTaggedLine(data, std::nullopt);
}

auto HashFunction::HashTaggedLine(std::string const& data,
                                  std::optional<TagCreator> tag_creator)
    const noexcept -> Hasher::HashDigest {
    auto hasher = MakeHasher();
    if (type_ == Type::GitSHA1 and tag_creator.has_value()) {
        hasher.Update(std::invoke(*tag_creator, data.size()));
    }
    hasher.Update(data);
    return std::move(hasher).Finalize();
}

auto HashFunction::HashBlobFile(std::filesystem::path const& path) const
    noexcept -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>> {
    return HashTaggedFile(path, CreateGitBlobTag);
}

auto HashFunction::HashTreeFile(std::filesystem::path const& path) const
    noexcept -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>> {
    return HashTaggedFile(path, CreateGitTreeTag);
}

auto HashFunction::HashTaggedFile(std::filesystem::path const& path,
                                  TagCreator const& tag_creator) const noexcept
    -> std::optional<std::pair<Hasher::HashDigest, std::uintmax_t>> {
    auto const size = std::filesystem::file_size(path);

    static constexpr std::size_t kChunkSize{4048};
    auto hasher = MakeHasher();
    if (type_ == Type::GitSHA1) {
        hasher.Update(std::invoke(tag_creator, size));
    }

    auto const to_read = IncrementalReader::FromFile(kChunkSize, path);
    if (not to_read.has_value()) {
        Logger::Log(LogLevel::Debug,
                    "Failed to create a reader for {}: {}",
                    path.string(),
                    to_read.error());
        return std::nullopt;
    }

    try {
        for (auto chunk : *to_read) {
            if (not chunk.has_value()) {
                Logger::Log(LogLevel::Debug,
                            "Error while trying to hash {}: {}",
                            path.string(),
                            chunk.error());
                return std::nullopt;
            }
            hasher.Update(std::string{*chunk});
        }
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Debug,
                    "Error while trying to hash {}: {}",
                    path.string(),
                    e.what());
        return std::nullopt;
    }
    return std::make_pair(std::move(hasher).Finalize(), size);
}
