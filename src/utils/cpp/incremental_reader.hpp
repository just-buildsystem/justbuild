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

#ifndef INCLUDED_SRC_UTILS_CPP_INCREMENTAL_READER_HPP
#define INCLUDED_SRC_UTILS_CPP_INCREMENTAL_READER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "gsl/gsl"
#include "src/utils/cpp/expected.hpp"

/// \brief Read data from source incrementally chunk by chunk.
/// - Ensures that chunks are exactly the specified size if EOF is not reached.
/// - Ensures no allocations happen while reading. Uses pre-allocated buffer and
/// utilizes std::string_view.
/// - Guarantees to return at least one chunk for an empty source.
class IncrementalReader final {
    class Iterator final {
      public:
        using value_type = expected<std::string_view, std::string>;
        using pointer = value_type*;
        using reference = value_type&;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        explicit Iterator(gsl::not_null<IncrementalReader const*> const& owner,
                          std::size_t offset) noexcept;

        auto operator*() const noexcept -> value_type;
        auto operator++() noexcept -> Iterator&;

        [[nodiscard]] friend auto operator==(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return lhs.owner_.get() == rhs.owner_.get() and
                   lhs.offset_ == rhs.offset_;
        }

        [[nodiscard]] friend auto operator!=(Iterator const& lhs,
                                             Iterator const& rhs) noexcept
            -> bool {
            return not(lhs == rhs);
        }

      private:
        // Store by pointer to allow copies:
        gsl::not_null<IncrementalReader const*> owner_;
        std::size_t offset_;
    };

  public:
    /// \brief Create IncrementalReader that uses the given file as the source
    /// of data.
    /// \param chunk_size       Size of chunk, must be greater than 0.
    /// \param path             File to read.
    /// \return Configured reader on success or an error message on failure.
    [[nodiscard]] static auto FromFile(
        std::size_t chunk_size,
        std::filesystem::path const& path) noexcept
        -> expected<IncrementalReader, std::string>;

    /// \brief Create IncrementalReader that uses the given string as the source
    /// of data.
    /// \param chunk_size       Size of chunk, must be greater than 0.
    /// \param data             String to read.
    /// \return Configured reader on success or an error message on failure.
    [[nodiscard]] static auto FromMemory(
        std::size_t chunk_size,
        gsl::not_null<std::string const*> const& data) noexcept
        -> expected<IncrementalReader, std::string>;

    [[nodiscard]] auto GetContentSize() const noexcept -> std::size_t {
        return content_size_;
    }

    /// \brief Create an iterator corresponding to the given offset. If the
    /// offset exceeds the maximum content size, it is adjusted.
    [[nodiscard]] auto make_iterator(std::size_t offset) const noexcept
        -> Iterator {
        return Iterator{this, std::min(offset, GetEndOffset())};
    }

    [[nodiscard]] auto begin() const& noexcept -> Iterator {
        return make_iterator(/*offset=*/0);
    }

    [[nodiscard]] auto end() const& noexcept -> Iterator {
        return make_iterator(GetEndOffset());
    }

  private:
    using FileSource = std::shared_ptr<std::FILE>;
    using MemorySource = gsl::not_null<std::string const*>;
    using ContentSource = std::variant<FileSource, MemorySource>;

    std::size_t chunk_size_;
    std::size_t content_size_;
    ContentSource content_;
    mutable std::string buffer_;

    explicit IncrementalReader(std::size_t chunk_size,
                               std::size_t content_size,
                               ContentSource content,
                               std::string buffer) noexcept
        : chunk_size_{chunk_size},
          content_size_{content_size},
          content_{std::move(content)},
          buffer_{std::move(buffer)} {}

    [[nodiscard]] auto ReadChunk(std::size_t offset) const noexcept
        -> expected<std::string_view, std::string>;

    [[nodiscard]] auto ReadFromFile(FileSource const& file, std::size_t offset)
        const -> expected<std::string_view, std::string>;

    [[nodiscard]] auto ReadFromMemory(MemorySource const& data,
                                      std::size_t offset) const
        -> expected<std::string_view, std::string>;

    /// \brief Obtain offset corresponding to the end of content. The content
    /// size is shifted by 1 character to properly handle empty sources.
    [[nodiscard]] auto GetEndOffset() const noexcept -> std::size_t {
        return content_size_ + 1;
    }
};

#endif  // INCLUDED_SRC_UTILS_CPP_INCREMENTAL_READER_HPP
