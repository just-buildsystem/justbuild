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

#include "src/buildtool/common/artifact_blob.hpp"

#include <exception>
#include <optional>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/in_place_visitor.hpp"

namespace {
[[nodiscard]] auto ReadFromFile(std::filesystem::path const& file) noexcept
    -> std::shared_ptr<std::string const> {
    auto content = FileSystemManager::ReadFile(file);
    if (not content.has_value()) {
        return nullptr;
    }
    return std::make_shared<std::string const>(*std::move(content));
}
}  // namespace

auto ArtifactBlob::FromMemory(HashFunction hash_function,
                              ObjectType type,
                              std::string content) noexcept
    -> expected<ArtifactBlob, std::string> {
    try {
        auto digest = IsTreeObject(type)
                          ? ArtifactDigestFactory::HashDataAs<ObjectType::Tree>(
                                hash_function, content)
                          : ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                                hash_function, content);
        return ArtifactBlob{
            std::move(digest),
            std::make_shared<std::string const>(std::move(content)),
            IsExecutableObject(type)};
    } catch (const std::exception& e) {
        return unexpected{fmt::format(
            "ArtifactBlob::FromMemory: Got an exception:\n{}", e.what())};
    } catch (...) {
        return unexpected<std::string>{
            "ArtifactBlob::FromMemory: Got an unknown exception"};
    }
}

auto ArtifactBlob::FromFile(HashFunction hash_function,
                            ObjectType type,
                            std::filesystem::path file) noexcept
    -> expected<ArtifactBlob, std::string> {
    try {
        if (not FileSystemManager::IsFile(file)) {
            return unexpected{
                fmt::format("ArtifactBlob::FromFile: Not a regular file:\n{}",
                            file.string())};
        }
        auto digest = IsTreeObject(type)
                          ? ArtifactDigestFactory::HashFileAs<ObjectType::Tree>(
                                hash_function, file)
                          : ArtifactDigestFactory::HashFileAs<ObjectType::File>(
                                hash_function, file);
        if (not digest.has_value()) {
            return unexpected{fmt::format(
                "ArtifactBlob::FromFile: Failed to hash {}", file.string())};
        }
        return ArtifactBlob{
            *std::move(digest), std::move(file), IsExecutableObject(type)};
    } catch (const std::exception& e) {
        return unexpected{fmt::format(
            "ArtifactBlob::FromFile: Got an exception while processing {}:\n{}",
            file.string(),
            e.what())};
    } catch (...) {
        return unexpected{
            fmt::format("ArtifactBlob::FromFile: Got an unknown exception "
                        "while processing {}",
                        file.string())};
    }
}

auto ArtifactBlob::ReadContent() const noexcept
    -> std::shared_ptr<std::string const> {
    using Result = std::shared_ptr<std::string const>;
    static constexpr InPlaceVisitor kVisitor{
        [](InMemory const& value) -> Result { return value; },
        [](InFile const& value) -> Result { return ::ReadFromFile(value); },
    };
    try {
        return std::visit(kVisitor, content_);
    } catch (...) {
        return nullptr;
    }
}

auto ArtifactBlob::ReadIncrementally(std::size_t chunk_size) const& noexcept
    -> expected<IncrementalReader, std::string> {
    using Result = expected<IncrementalReader, std::string>;
    const InPlaceVisitor visitor{
        [chunk_size](InMemory const& value) -> Result {
            if (value == nullptr) {
                return unexpected<std::string>{
                    "ArtifactBlob::ReadIncrementally: missing memory source"};
            }
            return IncrementalReader::FromMemory(chunk_size, value.get());
        },
        [chunk_size](InFile const& value) -> Result {
            return IncrementalReader::FromFile(chunk_size, value);
        },
    };
    try {
        return std::visit(visitor, content_);
    } catch (std::exception const& e) {
        return unexpected{fmt::format(
            "ArtifactBlob::ReadIncrementally: Got an exception:\n{}",
            e.what())};
    } catch (...) {
        return unexpected<std::string>{
            "ArtifactBlob::ReadIncrementally: Got an unknown exception"};
    }
}

namespace std {
auto hash<ArtifactBlob>::operator()(ArtifactBlob const& blob) const noexcept
    -> std::size_t {
    std::size_t seed{};
    hash_combine(&seed, blob.GetDigest());
    hash_combine(&seed, blob.IsExecutable());
    return seed;
}
}  // namespace std
