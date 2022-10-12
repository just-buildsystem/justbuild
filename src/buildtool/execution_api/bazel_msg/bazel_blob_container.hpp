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

#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/utils/cpp/concepts.hpp"

namespace detail {

// Interface for transforming iteratee for wrapped_iterators
template <class T_Iteratee, class T_Iterator>
struct wrapped_iterator_transform {
    [[nodiscard]] virtual auto operator()(T_Iterator const&) const& noexcept
        -> T_Iteratee const& = 0;
    [[nodiscard]] auto operator()(T_Iterator const&) && noexcept = delete;
};

// Wrap iterator from read-only container with custom transform. This class
// represents a read-only iterable view with an implicit transform operation.
template <class T_Iteratee,
          class T_Iterator,
          derived_from<wrapped_iterator_transform<T_Iteratee, T_Iterator>>
              T_Transform>
class wrapped_iterator {
  public:
    wrapped_iterator(T_Iterator it, T_Transform&& transform) noexcept
        : it_{std::move(it)}, transform_{std::move(transform)} {}
    wrapped_iterator(wrapped_iterator const& other) noexcept = default;
    wrapped_iterator(wrapped_iterator&& other) noexcept = default;
    ~wrapped_iterator() noexcept = default;

    auto operator=(wrapped_iterator const& other) noexcept
        -> wrapped_iterator& = default;
    auto operator=(wrapped_iterator&& other) noexcept
        -> wrapped_iterator& = default;

    auto operator++() noexcept -> wrapped_iterator& {
        ++it_;
        return *this;
    }

    auto operator++(int) noexcept -> wrapped_iterator {
        wrapped_iterator r = *this;
        ++(*this);
        return r;
    }

    [[nodiscard]] auto operator==(wrapped_iterator other) const noexcept
        -> bool {
        return it_ == other.it_;
    }
    [[nodiscard]] auto operator!=(wrapped_iterator other) const noexcept
        -> bool {
        return not(*this == other);
    }
    [[nodiscard]] auto operator*() const noexcept -> T_Iteratee const& {
        return transform_(it_);
    }
    using difference_type = typename T_Iterator::difference_type;
    using value_type = T_Iteratee;
    using pointer = T_Iteratee const*;
    using reference = T_Iteratee const&;
    using iterator_category = std::forward_iterator_tag;

  private:
    T_Iterator it_;
    T_Transform transform_;
};

}  // namespace detail

/// \brief Container for Blobs
/// Can be used to iterate over digests or subset of blobs with certain digest.
class BlobContainer {
    using underlaying_map_t = std::unordered_map<bazel_re::Digest, BazelBlob>;
    using item_iterator = underlaying_map_t::const_iterator;

    // transform underlaying_map_t::value_type to BazelBlob
    struct item_to_blob
        : public detail::wrapped_iterator_transform<BazelBlob, item_iterator> {
      public:
        auto operator()(item_iterator const& it) const& noexcept
            -> BazelBlob const& final {
            return it->second;
        }
    };

  public:
    class iterator : public detail::wrapped_iterator<BazelBlob,
                                                     item_iterator,
                                                     item_to_blob> {
        friend class BlobContainer;
        explicit iterator(item_iterator const& it) noexcept
            : wrapped_iterator{it, item_to_blob{}} {}
    };

    /// \brief Iterable read-only list for Digests
    class DigestList {
        friend class BlobContainer;

        // transform underlaying_map_t::value_type to Digest
        struct item_to_digest
            : public detail::wrapped_iterator_transform<bazel_re::Digest,
                                                        item_iterator> {
          public:
            auto operator()(item_iterator const& it) const& noexcept
                -> bazel_re::Digest const& final {
                return it->first;
            }
        };

      public:
        /// \brief Read-only iterator for DigestList
        class iterator : public detail::wrapped_iterator<bazel_re::Digest,
                                                         item_iterator,
                                                         item_to_digest> {
          public:
            explicit iterator(item_iterator const& it) noexcept
                : wrapped_iterator{it, item_to_digest{}} {}
        };

        /// \brief Obtain start iterator for DigestList
        [[nodiscard]] auto begin() const noexcept -> iterator {
            return iterator(blobs_->cbegin());
        }

        /// \brief Obtain end iterator for DigestList
        [[nodiscard]] auto end() const noexcept -> iterator {
            return iterator(blobs_->cend());
        }

      private:
        gsl::not_null<underlaying_map_t const*> blobs_;

        explicit DigestList(underlaying_map_t const& blobs) noexcept
            : blobs_{&blobs} {}
    };

    /// \brief Iterable read-only list for Blobs related to given Digests
    class RelatedBlobList {
        friend class BlobContainer;
        using digest_iterator = std::vector<bazel_re::Digest>::const_iterator;

        // transform Digest to BazelBlob
        struct digest_to_blob
            : public detail::wrapped_iterator_transform<BazelBlob,
                                                        digest_iterator> {
          public:
            explicit digest_to_blob(
                gsl::not_null<underlaying_map_t const*> blobs) noexcept
                : blobs_{std::move(blobs)} {}
            digest_to_blob(digest_to_blob const& other) noexcept = default;
            digest_to_blob(digest_to_blob&& other) noexcept = default;
            ~digest_to_blob() noexcept = default;

            auto operator=(digest_to_blob const& other) noexcept
                -> digest_to_blob& = default;
            auto operator=(digest_to_blob&& other) noexcept
                -> digest_to_blob& = default;

            auto operator()(digest_iterator const& it) const& noexcept
                -> BazelBlob const& final {
                try {
                    return blobs_->at(*it);
                } catch (std::exception const&) {
                    return kEmpty;
                }
            }

          private:
            static inline BazelBlob const kEmpty{bazel_re::Digest{},
                                                 std::string{}};
            gsl::not_null<underlaying_map_t const*> blobs_;
        };

      public:
        /// \brief Read-only iterator for RelatedBlobList
        class iterator : public detail::wrapped_iterator<BazelBlob,
                                                         digest_iterator,
                                                         digest_to_blob> {
          public:
            iterator(
                digest_iterator const& it,
                gsl::not_null<underlaying_map_t const*> const& blobs) noexcept
                : wrapped_iterator{it, digest_to_blob{blobs}} {}
        };

        /// \brief Obtain start iterator for RelatedBlobList
        [[nodiscard]] auto begin() const noexcept -> iterator {
            return iterator(digests_.cbegin(), blobs_);
        }

        /// \brief Obtain end iterator for RelatedBlobList
        [[nodiscard]] auto end() const noexcept -> iterator {
            return iterator(digests_.cend(), blobs_);
        }

      private:
        std::vector<bazel_re::Digest> digests_;
        gsl::not_null<underlaying_map_t const*> blobs_;

        RelatedBlobList(underlaying_map_t const& blobs,
                        std::vector<bazel_re::Digest> digests) noexcept
            : digests_{std::move(digests)}, blobs_{&blobs} {}
    };

    BlobContainer() noexcept = default;
    explicit BlobContainer(std::vector<BazelBlob> blobs) {
        blobs_.reserve(blobs.size());
        for (auto& blob : blobs) {
            blobs_.emplace(blob.digest, std::move(blob));
        }
    }

    /// \brief Emplace new BazelBlob to container.
    void Emplace(BazelBlob&& blob) {
        blobs_.emplace(blob.digest, std::move(blob));
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

    /// \brief Obtain iterable list of Digests from container.
    [[nodiscard]] auto Digests() const noexcept -> DigestList {
        return DigestList{blobs_};
    }

    /// \brief Obtain iterable list of BazelBlobs related to Digests.
    /// \param[in] related Related Digests
    [[nodiscard]] auto RelatedBlobs(
        std::vector<bazel_re::Digest> const& related) const noexcept
        -> RelatedBlobList {
        return RelatedBlobList{blobs_, related};
    };

    [[nodiscard]] auto begin() const noexcept -> iterator {
        return iterator{blobs_.begin()};
    }

    [[nodiscard]] auto end() const noexcept -> iterator {
        return iterator{blobs_.end()};
    }

  private:
    underlaying_map_t blobs_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_CONTAINER_HPP
