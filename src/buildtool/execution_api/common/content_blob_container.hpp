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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>  //std::move
#include <vector>

#include "gsl/gsl"
#include "src/utils/cpp/transformed_range.hpp"

template <typename TDigest>
struct ContentBlob final {
    ContentBlob(TDigest mydigest, std::string mydata, bool is_exec) noexcept
        : digest{std::move(mydigest)},
          data{std::make_shared<std::string>(std::move(mydata))},
          is_exec{is_exec} {}

    ContentBlob(TDigest mydigest,
                gsl::not_null<std::shared_ptr<std::string>> const& mydata,
                bool is_exec) noexcept
        : digest{std::move(mydigest)}, data(mydata), is_exec{is_exec} {}

    TDigest digest{};
    std::shared_ptr<std::string> data{};
    bool is_exec{};
};

template <typename TDigest>
class ContentBlobContainer final {
  public:
    using DigestType = TDigest;
    using BlobType = ContentBlob<TDigest>;

    ContentBlobContainer() noexcept = default;
    explicit ContentBlobContainer(std::vector<BlobType> blobs) {
        blobs_.reserve(blobs.size());
        for (auto& blob : blobs) {
            this->Emplace(std::move(blob));
        }
    }

    /// \brief Emplace new Blob to container.
    void Emplace(BlobType&& blob) {
        DigestType digest = blob.digest;
        if (auto res = blobs_.emplace(std::move(digest), std::move(blob));
            res.second) {
            // only count size if blob was actually added
            content_size_ += res.first->second.data->size();
        }
    }

    /// \brief Clear all Blobs from container.
    void Clear() noexcept {
        blobs_.clear();
        content_size_ = 0;
    }

    /// \brief Number of Blobs in container.
    [[nodiscard]] auto Size() const noexcept -> std::size_t {
        return blobs_.size();
    }

    /// \brief Collective size of the stored content in container.
    [[nodiscard]] auto ContentSize() const noexcept -> std::size_t {
        return content_size_;
    }

    /// \brief Is equivalent BazelBlob (with same Digest) in container.
    /// \param[in] blob BazelBlob to search equivalent BazelBlob for
    [[nodiscard]] auto Contains(BlobType const& blob) const noexcept -> bool {
        return blobs_.contains(blob.digest);
    }

    /// \brief Obtain iterable list of Blobs from container.
    [[nodiscard]] auto Blobs() const noexcept {
        auto converter = [](auto const& p) -> BlobType const& {
            return p.second;
        };
        return TransformedRange{
            blobs_.cbegin(), blobs_.cend(), std::move(converter)};
    }

    /// \brief Obtain iterable list of Digests from container.
    [[nodiscard]] auto Digests() const noexcept {
        auto converter = [](auto const& p) -> DigestType const& {
            return p.first;
        };
        return TransformedRange{
            blobs_.cbegin(), blobs_.cend(), std::move(converter)};
    }

    /// \brief Obtain iterable list of BazelBlobs related to Digests.
    /// \param[in] related Related Digests
    [[nodiscard]] auto RelatedBlobs(
        std::vector<DigestType> const& related) const noexcept {
        auto converter = [this](auto const& digest) -> BlobType const& {
            return blobs_.at(digest);
        };
        return TransformedRange{
            related.begin(), related.end(), std::move(converter)};
    };

  private:
    std::unordered_map<DigestType, BlobType> blobs_{};
    std::size_t content_size_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP
