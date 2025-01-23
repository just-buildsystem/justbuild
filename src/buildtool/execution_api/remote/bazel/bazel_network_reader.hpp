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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP

#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_cas_client.hpp"
#include "src/buildtool/file_system/git_repo.hpp"

class BazelNetworkReader final {
    class IncrementalReader;

  public:
    using DumpCallback = std::function<bool(std::string const&)>;

    explicit BazelNetworkReader(
        std::string instance_name,
        gsl::not_null<BazelCasClient const*> const& cas,
        gsl::not_null<HashFunction const*> const& hash_function) noexcept;

    BazelNetworkReader(
        BazelNetworkReader&& other,
        std::optional<ArtifactDigest> request_remote_tree) noexcept;

    [[nodiscard]] auto ReadDirectory(ArtifactDigest const& digest)
        const noexcept -> std::optional<bazel_re::Directory>;

    [[nodiscard]] auto ReadGitTree(ArtifactDigest const& digest) const noexcept
        -> std::optional<GitRepo::tree_entries_t>;

    [[nodiscard]] auto DumpRawTree(Artifact::ObjectInfo const& info,
                                   DumpCallback const& dumper) const noexcept
        -> bool;

    [[nodiscard]] auto DumpBlob(Artifact::ObjectInfo const& info,
                                DumpCallback const& dumper) const noexcept
        -> bool;

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] auto StageBlobTo(
        Artifact::ObjectInfo const& /*info*/,
        std::filesystem::path const& /*output*/) const noexcept -> bool {
        return false;  // not implemented
    }

    [[nodiscard]] auto IsNativeProtocol() const noexcept -> bool;

    [[nodiscard]] auto ReadSingleBlob(ArtifactDigest const& digest)
        const noexcept -> std::optional<ArtifactBlob>;

    [[nodiscard]] auto ReadIncrementally(
        std::vector<ArtifactDigest> const& digests) const noexcept
        -> IncrementalReader;

  private:
    using DirectoryMap =
        std::unordered_map<ArtifactDigest, bazel_re::Directory>;

    std::string const instance_name_;
    BazelCasClient const& cas_;
    HashFunction const& hash_function_;
    std::optional<DirectoryMap> auxiliary_map_;

    [[nodiscard]] auto MakeAuxiliaryMap(
        std::vector<bazel_re::Directory>&& full_tree) const noexcept
        -> std::optional<DirectoryMap>;

    [[nodiscard]] auto ReadSingleBlob(bazel_re::Digest const& digest)
        const noexcept -> std::optional<ArtifactBlob>;

    [[nodiscard]] auto BatchReadBlobs(
        std::vector<bazel_re::Digest> const& blobs) const noexcept
        -> std::vector<ArtifactBlob>;

    [[nodiscard]] auto ReadIncrementally(std::vector<bazel_re::Digest> digests)
        const noexcept -> IncrementalReader;

    [[nodiscard]] auto Validate(BazelBlob const& blob) const noexcept
        -> std::optional<HashInfo>;
};

class BazelNetworkReader::IncrementalReader final {
  public:
    IncrementalReader(BazelNetworkReader const& owner,
                      std::vector<bazel_re::Digest> digests) noexcept
        : owner_(owner), digests_(std::move(digests)) {}

    class Iterator final {
      public:
        using value_type = std::vector<ArtifactBlob>;
        using pointer = value_type*;
        using reference = value_type&;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        Iterator(BazelNetworkReader const& owner,
                 std::vector<bazel_re::Digest>::const_iterator begin,
                 std::vector<bazel_re::Digest>::const_iterator end) noexcept;

        auto operator*() const noexcept -> value_type;
        auto operator++() noexcept -> Iterator&;

        [[nodiscard]] friend auto operator==(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return lhs.begin_ == rhs.begin_ and lhs.end_ == rhs.end_ and
                   lhs.current_ == rhs.current_;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return not(lhs == rhs);
        }

      private:
        BazelNetworkReader const& owner_;
        std::vector<bazel_re::Digest>::const_iterator begin_;
        std::vector<bazel_re::Digest>::const_iterator end_;
        std::vector<bazel_re::Digest>::const_iterator current_;
    };

    [[nodiscard]] auto begin() const noexcept {
        return Iterator{owner_, digests_.begin(), digests_.end()};
    }

    [[nodiscard]] auto end() const noexcept {
        return Iterator{owner_, digests_.end(), digests_.end()};
    }

  private:
    BazelNetworkReader const& owner_;
    std::vector<bazel_re::Digest> digests_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_BAZEL_BAZEL_TREE_READER_HPP
