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

#include "src/buildtool/storage/file_chunker.hpp"

#include <array>
#include <random>

#include "gsl/gsl"

namespace {

// Mask values taken from algorithm 2 of the paper
// https://ieeexplore.ieee.org/document/9055082.
constexpr std::uint64_t kMaskS{0x4444d9f003530000ULL};  // 19 '1' bits
constexpr std::uint64_t kMaskL{0x4444d90003530000ULL};  // 15 '1' bits

// Predefined array of 256 random 64-bit integers, needs to be initialized.
constexpr std::uint32_t kRandomTableSize{256};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<std::uint64_t, kRandomTableSize> gear_table{};

}  // namespace

auto FileChunker::Initialize(std::uint32_t seed) noexcept -> void {
    std::mt19937_64 gen64(seed);
    for (auto& item : gear_table) {
        item = gen64();
    }
}

auto FileChunker::IsOpen() const noexcept -> bool {
    return stream_.is_open();
}

auto FileChunker::Finished() const noexcept -> bool {
    return stream_.eof() and pos_ == size_;
}

auto FileChunker::NextChunk() noexcept -> std::optional<std::string> {
    // Handle failed past read attempts from the stream.
    if (not stream_.good() and not stream_.eof()) {
        return std::nullopt;
    }

    // Ensure that at least max_chunk_size bytes are in the buffer, except if
    // end-of-file is reached.
    auto remaining = size_ - pos_;
    if (remaining < max_chunk_size_ and not stream_.eof()) {
        // Move the remaining bytes of the buffer to the front.
        buffer_.copy(&buffer_[0], remaining, pos_);
        auto ssize = static_cast<std::streamsize>(buffer_.size() - remaining);
        // Fill the buffer with stream content.
        stream_.read(&buffer_[remaining], ssize);
        if (not stream_.good() and not stream_.eof()) {
            return std::nullopt;
        }
        size_ = static_cast<std::size_t>(stream_.gcount()) + remaining;
        pos_ = 0;
    }

    // Handle finished chunking.
    if (pos_ == size_) {
        return std::nullopt;
    }

    auto off = NextChunkBoundary();
    auto chunk = buffer_.substr(pos_, off);
    pos_ += off;
    return chunk;
}

// Implementation of the FastCDC data deduplication algorithm described in
// algorithm 2 of the paper https://ieeexplore.ieee.org/document/9055082.
auto FileChunker::NextChunkBoundary() noexcept -> std::size_t {
    auto n = size_ - pos_;
    auto fp = 0ULL;
    auto i = min_chunk_size_;
    auto normal_size = average_chunk_size_;
    if (n <= min_chunk_size_) {
        return n;
    }
    if (n >= max_chunk_size_) {
        n = max_chunk_size_;
    }
    else if (n <= normal_size) {
        normal_size = n;
    }
    for (; i < normal_size; i++) {
        fp = (fp << 1U) +
             gsl::at(gear_table, static_cast<std::uint8_t>(buffer_[pos_ + i]));
        if ((fp & kMaskS) == 0) {
            return i;  // if the masked bits are all '0'
        }
    }
    for (; i < n; i++) {
        fp = (fp << 1U) +
             gsl::at(gear_table, static_cast<std::uint8_t>(buffer_[pos_ + i]));
        if ((fp & kMaskL) == 0) {
            return i;  // if the masked bits are all '0'
        }
    }
    return i;
}
