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

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/in_place_visitor.hpp"

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

auto ArtifactBlob::ReadContent() const noexcept
    -> std::shared_ptr<std::string const> {
    using Result = std::shared_ptr<std::string const>;
    static constexpr InPlaceVisitor kVisitor{
        [](InMemory const& value) -> Result { return value; },
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
