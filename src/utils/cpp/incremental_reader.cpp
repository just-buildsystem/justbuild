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

#include "src/utils/cpp/incremental_reader.hpp"

#include <cstdint>
#include <exception>
#include <string_view>

#include "fmt/core.h"
#include "src/utils/cpp/in_place_visitor.hpp"

namespace {
void DisposeFile(gsl::owner<std::FILE*> file) noexcept {
    if (file == nullptr) {
        return;
    }
    std::fclose(file);
}
}  // namespace

auto IncrementalReader::FromFile(std::size_t chunk_size,
                                 std::filesystem::path const& path) noexcept
    -> expected<IncrementalReader, std::string> {
    if (chunk_size == 0) {
        return unexpected<std::string>{
            "IncrementalReader: the chunk size cannot be 0"};
    }

    try {
        // Ensure this is a file:
        if (not std::filesystem::is_regular_file(path)) {
            return unexpected{fmt::format(
                "IncrementalReader: not a file :\n {} ", path.string())};
        }

        // Open file for reading:
        static constexpr std::string_view kReadBinary = "rb";
        auto file = std::shared_ptr<std::FILE>{
            std::fopen(path.c_str(), kReadBinary.data()), ::DisposeFile};
        if (file == nullptr) {
            return unexpected{
                fmt::format("IncrementalReader: failed to open the file:\n{}",
                            path.string())};
        }

        std::size_t const content_size = std::filesystem::file_size(path);
        return IncrementalReader{chunk_size,
                                 content_size,
                                 std::move(file),
                                 /*buffer=*/std::string(chunk_size, '\0')};
    } catch (std::exception const& e) {
        return unexpected{fmt::format(
            "IncrementalReader: While processing {}\ngot an exception: {}",
            path.string(),
            e.what())};
    } catch (...) {
        return unexpected{fmt::format(
            "IncrementalReader: While processing {}\ngot an unknown exception",
            path.string())};
    }
}

auto IncrementalReader::FromMemory(
    std::size_t chunk_size,
    gsl::not_null<std::string const*> const& data) noexcept
    -> expected<IncrementalReader, std::string> {
    if (chunk_size == 0) {
        return unexpected<std::string>{
            "IncrementalReader: the chunk size cannot be 0"};
    }

    try {
        // Reading from memory doesn't require a buffer. The resulting chunks
        // point at the content_ directly.
        return IncrementalReader{chunk_size,
                                 /*content_size=*/data->size(),
                                 data,
                                 /*buffer=*/std::string{}};
    } catch (...) {
        return unexpected<std::string>{
            "IncrementalReader: Got an unknown exception during creation from "
            "a string"};
    }
}

auto IncrementalReader::ReadChunk(std::size_t offset) const noexcept
    -> expected<std::string_view, std::string> {
    using Result = expected<std::string_view, std::string>;
    InPlaceVisitor const visitor{
        [this, offset](FileSource const& file) -> Result {
            return ReadFromFile(file, offset);
        },
        [this, offset](MemorySource const& data) -> Result {
            return ReadFromMemory(data, offset);
        },
    };

    try {
        return std::visit(visitor, content_);
    } catch (std::exception const& e) {
        return unexpected{fmt::format(
            "IncrementalReader: ReadChunk got an exception:\n{}", e.what())};
    } catch (...) {
        return unexpected<std::string>{
            "IncrementalReader: ReadChunk got an unknown exception"};
    }
}

auto IncrementalReader::ReadFromFile(FileSource const& file, std::size_t offset)
    const -> expected<std::string_view, std::string> {
    if (file == nullptr) {
        return unexpected<std::string>{
            "IncrementalReader: ReadFromFile: got corrupted file"};
    }

    if (std::fseek(file.get(), gsl::narrow<std::int64_t>(offset), SEEK_SET) !=
        0) {
        return unexpected<std::string>{
            "IncrementalReader: ReadFromFile: failed to set offset"};
    }

    std::size_t read = 0;
    while (std::feof(file.get()) == 0 and std::ferror(file.get()) == 0 and
           read < buffer_.size()) {
        read += std::fread(
            &buffer_[read], sizeof(char), buffer_.size() - read, file.get());
    }
    if (std::ferror(file.get()) != 0) {
        return unexpected{
            fmt::format("IncrementalReader: ReadFromFile: ferror {}",
                        std::ferror(file.get()))};
    }
    return std::string_view{buffer_.data(), read};
}

auto IncrementalReader::ReadFromMemory(MemorySource const& data,
                                       std::size_t offset) const
    -> expected<std::string_view, std::string> {
    if (data->empty()) {
        // NOLINTNEXTLINE(bugprone-string-constructor,-warnings-as-errors)
        return std::string_view{data->data(), 0};
    }

    std::size_t const read = std::min(chunk_size_, data->size() - offset);
    return std::string_view{&data->at(offset), read};
}

IncrementalReader::Iterator::Iterator(
    gsl::not_null<IncrementalReader const*> const& owner,
    std::size_t offset) noexcept
    : owner_{owner}, offset_{offset} {}

auto IncrementalReader::Iterator::operator*() const noexcept
    -> expected<std::string_view, std::string> {
    return owner_->ReadChunk(offset_);
}

auto IncrementalReader::Iterator::operator++() noexcept
    -> IncrementalReader::Iterator& {
    offset_ += owner_->chunk_size_;
    if (offset_ >= owner_->content_size_) {
        offset_ = owner_->GetEndOffset();
    }
    return *this;
}
