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

#ifndef INCLUDED_SRC_OTHER_TOOLS_UTILS_ARCHIVE_OPS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_UTILS_ARCHIVE_OPS_HPP

#include <filesystem>
#include <optional>

#include "gsl/gsl"

extern "C" {
using archive = struct archive;
using archive_entry = struct archive_entry;
}

enum class ArchiveType : size_t {
    Zip,
    _7Zip,
    Tar,  // uncompressed
    TarGz,
    TarBz2,
    TarXz,
    TarLz,
    TarLzma,
    TarAuto  // autodetect tarball-type archives
};

/// \brief Class handling archiving and unarchiving operations via libarchive
class ArchiveOps {
  public:
    /// \brief Create archive of given type from file or directory at
    /// source. All paths will be takes relative to current directory.
    /// Destination folder is the current directory. Archive is stored under
    /// given name. Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static CreateArchive(
        ArchiveType type,
        std::string const& name,
        std::filesystem::path const& source) noexcept
        -> std::optional<std::string>;

    /// \brief Create archive of given type from file or directory at source and
    /// store it in destDir folder under given name. All paths will be taken as
    /// relative to the current directory. Destination directory is created if
    /// not present. Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static CreateArchive(
        ArchiveType type,
        std::string const& name,
        std::filesystem::path const& source,
        std::filesystem::path const& destDir) noexcept
        -> std::optional<std::string>;

    /// \brief Extract archive pointed to by source into destDir folder. The
    /// destination folder is the current directory and the type of archive is
    /// specified from currently supported formats: tar, zip, tar.gz, tar.bz2.
    /// Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static ExtractArchive(
        ArchiveType type,
        std::filesystem::path const& source) noexcept
        -> std::optional<std::string>;

    /// \brief Extract archive pointed to by source into destDir folder. The
    /// type of archive is specified from currently supported formats: tar, zip,
    /// tar.gz, tar.bz2. Returns nullopt on success, or an error string if
    /// failure.
    [[nodiscard]] auto static ExtractArchive(
        ArchiveType type,
        std::filesystem::path const& source,
        std::filesystem::path const& destDir) noexcept
        -> std::optional<std::string>;

  private:
    /// \brief Copy entry into archive object.
    /// Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static WriteEntry(archive_entry* entry, archive* aw)
        -> std::optional<std::string>;

    /// \brief Copy data blocks from one archive object to another.
    /// Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static CopyData(archive* ar, archive* aw)
        -> std::optional<std::string>;

    /// \brief Set up the appropriate supported format for writing an archive.
    /// Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static EnableWriteFormats(archive* aw, ArchiveType type)
        -> std::optional<std::string>;

    /// \brief Set up the supported formats for reading in an archive.
    /// Returns nullopt on success, or an error string if failure.
    [[nodiscard]] auto static EnableReadFormats(archive* ar, ArchiveType type)
        -> std::optional<std::string>;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_UTILS_ARCHIVE_OPS_HPP
