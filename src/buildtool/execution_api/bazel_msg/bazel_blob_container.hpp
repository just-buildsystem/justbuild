// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_CONTAINER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_CONTAINER_HPP

#include <cstddef>
#include <unordered_map>
#include <utility>  // std::pair via auto
#include <vector>

#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/utils/cpp/transformed_range.hpp"

/// \brief Container for Blobs
/// Can be used to iterate over digests or subset of blobs with certain digest.
class BlobContainer {
    using underlaying_map_t = std::unordered_map<bazel_re::Digest, BazelBlob>;

  public:
    BlobContainer() noexcept = default;
    explicit BlobContainer(std::vector<BazelBlob> blobs) {
        blobs_.reserve(blobs.size());
        for (auto& blob : blobs) {
            this->Emplace(std::move(blob));
        }
    }

    /// \brief Emplace new BazelBlob to container.
    void Emplace(BazelBlob&& blob) {
        bazel_re::Digest digest = blob.digest;
        blobs_.emplace(std::move(digest), std::move(blob));
    }

    /// \brief Clear all BazelBlobs from container.
    void Clear() noexcept { return blobs_.clear(); }

    /// \brief Number of BazelBlobs in container.
    [[nodiscard]] auto Size() const noexcept -> std::size_t {
        return blobs_.size();
    }

    /// \brief Is equivalent BazelBlob (with same Digest) in container.
    /// \param[in] blob BazelBlob to search equivalent BazelBlob for
    [[nodiscard]] auto Contains(BazelBlob const& blob) const noexcept -> bool {
        return blobs_.contains(blob.digest);
    }

    /// \brief Obtain iterable list of Blobs from container.
    [[nodiscard]] auto Blobs() const noexcept {
        auto converter = [](auto const& p) -> BazelBlob const& {
            return p.second;
        };
        return TransformedRange{
            blobs_.cbegin(), blobs_.cend(), std::move(converter)};
    }

    /// \brief Obtain iterable list of Digests from container.
    [[nodiscard]] auto Digests() const noexcept {
        auto converter = [](auto const& p) -> bazel_re::Digest const& {
            return p.first;
        };
        return TransformedRange{
            blobs_.cbegin(), blobs_.cend(), std::move(converter)};
    }

    /// \brief Obtain iterable list of BazelBlobs related to Digests.
    /// \param[in] related Related Digests
    [[nodiscard]] auto RelatedBlobs(
        std::vector<bazel_re::Digest> const& related) const noexcept {
        auto converter = [this](auto const& digest) -> BazelBlob const& {
            return blobs_.at(digest);
        };
        return TransformedRange{
            related.begin(), related.end(), std::move(converter)};
    };

  private:
    underlaying_map_t blobs_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_CONTAINER_HPP
