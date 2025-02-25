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

#ifndef INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/incremental_reader.hpp"

class ArtifactBlob final {
  public:
    /// \brief Create ArtifactBlob and keep the given content in memory. The
    /// content is hashed based on the given hash function and ObjectType.
    /// \param hash_function    Hash function that must be used for hashing.
    /// \param type             Type of the content.
    /// \param content          String to be stored
    /// \return Valid ArtifactBlob on success or an error message on failure.
    [[nodiscard]] static auto FromMemory(HashFunction hash_function,
                                         ObjectType type,
                                         std::string content) noexcept
        -> expected<ArtifactBlob, std::string>;

    /// \brief Create ArtifactBlob based on the existing file. The content is
    /// hashed based on the given hash function and ObjectType.
    /// \param hash_function    Hash function that must be used for hashing.
    /// \param type             Type of the content.
    /// \param file             Existing file to be used as the source of
    /// content.
    /// \return Valid ArtifactBlob on success or an error message on failure.
    [[nodiscard]] static auto FromFile(HashFunction hash_function,
                                       ObjectType type,
                                       std::filesystem::path file) noexcept
        -> expected<ArtifactBlob, std::string>;

    [[nodiscard]] auto operator==(ArtifactBlob const& other) const noexcept
        -> bool {
        return digest_ == other.digest_ and
               is_executable_ == other.is_executable_;
    }

    /// \brief Obtain the digest of the content.
    [[nodiscard]] auto GetDigest() const noexcept -> ArtifactDigest const& {
        return digest_;
    }

    /// \brief Obtain the size of the content.
    [[nodiscard]] auto GetContentSize() const noexcept -> std::size_t {
        return digest_.size();
    }

    /// \brief Read the content from source. This operation may result in the
    /// entire file being read into memory.
    [[nodiscard]] auto ReadContent() const noexcept
        -> std::shared_ptr<std::string const>;

    /// \brief Create an IncrementalReader that uses this ArtifactBlob's content
    /// source.
    /// \param chunk_size   Size of chunk, must be greater than 0.
    /// \return Valid IncrementalReader on success or an error message on
    /// failure.
    [[nodiscard]] auto ReadIncrementally(std::size_t chunk_size) const& noexcept
        -> expected<IncrementalReader, std::string>;

    /// \brief Set executable permission.
    void SetExecutable(bool is_executable) noexcept {
        is_executable_ = is_executable;
    }

    /// \brief Obtain executable permission.
    [[nodiscard]] auto IsExecutable() const noexcept -> bool {
        return is_executable_;
    }

  private:
    using InMemory = std::shared_ptr<std::string const>;
    using InFile = std::filesystem::path;
    using ContentSource = std::variant<InMemory, InFile>;

    ArtifactDigest digest_;
    ContentSource content_;
    bool is_executable_;

    explicit ArtifactBlob(ArtifactDigest digest,
                          ContentSource content,
                          bool is_executable) noexcept
        : digest_{std::move(digest)},
          content_{std::move(content)},
          is_executable_{is_executable} {}
};

namespace std {
template <>
struct hash<ArtifactBlob> {
    [[nodiscard]] auto operator()(ArtifactBlob const& blob) const noexcept
        -> std::size_t;
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_COMMON_ARTIFACT_BLOB_HPP
