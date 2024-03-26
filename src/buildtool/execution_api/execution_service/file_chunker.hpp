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

#ifndef INCLUDED_SRC_EXECUTION_API_EXECUTION_SERVICE_FILE_CHUNKER_HPP
#define INCLUDED_SRC_EXECUTION_API_EXECUTION_SERVICE_FILE_CHUNKER_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

/// @brief This class provides content-defined chunking for a file stream. It
/// allows to split a file stream into variable-sized chunks based on its data
/// content. In contrast to fixed-sized chunking, which splits a data stream
/// into chunks of fixed size, it is not prone to the data-shifting problem. In
/// order to assemble the resulting file, the delivered chunks have to be
/// concatenated in order.
///
/// A read buffer is used to progressively process the file content instead of
/// reading the entire file content in memory.
class FileChunker {
    static constexpr std::uint32_t kDefaultChunkSize{1024 * 8};  // 8 KB
    static constexpr std::uint32_t kDefaultSeed{0};

  public:
    /// @brief Create an instance of the file chunker for a given file.
    /// @param path                 The path to the file to be splitted.
    /// @param average_chunk_size   Targeted average chunk size in bytes
    ///                             (default: 8 KB).
    explicit FileChunker(std::filesystem::path const& path,
                         std::uint32_t average_chunk_size = kDefaultChunkSize)
        // According to section 4.1 of the paper
        // https://ieeexplore.ieee.org/document/9055082, maximum and minimum
        // chunk sizes are configured to the 8x and the 1/4x of the average
        // chunk size.
        : min_chunk_size_(average_chunk_size >> 2U),
          average_chunk_size_(average_chunk_size),
          max_chunk_size_(average_chunk_size << 3U),
          stream_{path, std::ios::in | std::ios::binary} {
        // The buffer size needs to be at least max_chunk_size_ large, otherwise
        // max_chunk_size_ is not fully exhausted and the buffer size determines
        // the maximum chunk size.
        buffer_.resize(max_chunk_size_ << 4U);
    }

    FileChunker() noexcept = delete;
    ~FileChunker() noexcept = default;
    FileChunker(FileChunker const& other) noexcept = delete;
    FileChunker(FileChunker&& other) noexcept = delete;
    auto operator=(FileChunker const& other) noexcept = delete;
    auto operator=(FileChunker&& other) noexcept = delete;

    /// @brief Check if the underlying file is open.
    /// @return True if the file was opened successfully, false otherwise.
    [[nodiscard]] auto IsOpen() const noexcept -> bool;

    /// @brief Check if chunking of the file stream was done successfully.
    /// @return True if chunking was successful, false otherwise.
    [[nodiscard]] auto Finished() const noexcept -> bool;

    /// @brief Fetch the next chunk from the file stream.
    /// @return The next chunk of the file stream.
    [[nodiscard]] auto NextChunk() noexcept -> std::optional<std::string>;

    /// @brief Initialize random number table used by the chunking algorithm.
    /// @param seed Some random seed.
    static auto Initialize(std::uint32_t seed = kDefaultSeed) noexcept -> void;

  private:
    // Different chunk size parameters, defined in number of bytes.
    const std::uint32_t min_chunk_size_{};
    const std::uint32_t average_chunk_size_{};
    const std::uint32_t max_chunk_size_{};
    std::ifstream stream_{};  // File stream to be splitted.
    std::string buffer_{};    // Buffer for the file content.
    std::size_t size_{0};     // Current size of the buffer.
    std::size_t pos_{0};      // Current read position within the buffer.

    /// @brief Find the next chunk boundary from the current read position
    /// within the buffer.
    /// @return The position of the next chunk boundary.
    [[nodiscard]] auto NextChunkBoundary() noexcept -> std::size_t;
};

#endif  // INCLUDED_SRC_EXECUTION_API_EXECUTION_SERVICE_FILE_CHUNKER_HPP
