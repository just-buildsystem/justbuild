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

#include "src/other_tools/utils/archive_ops.hpp"

#include "src/buildtool/file_system/file_system_manager.hpp"

extern "C" {
#include <archive.h>
#include <archive_entry.h>
}

namespace {

/// \brief Default block size for archive extraction.
constexpr size_t kArchiveBlockSize = 10240;

/// \brief Clean-up function for archive entry objects.
void archive_entry_cleanup(archive_entry* entry) {
    if (entry != nullptr) {
        archive_entry_free(entry);
    }
}

/// \brief Clean-up function for archive objects open for writing.
void archive_write_closer(archive* a_out) {
    if (a_out != nullptr) {
        archive_write_close(a_out);  // libarchive handles non-openness
        archive_write_free(a_out);   // also do cleanup!
    }
}

/// \brief Clean-up function for archive objects open for reading.
void archive_read_closer(archive* a_in) {
    if (a_in != nullptr) {
        archive_read_close(a_in);  // libarchive handles non-openness
        archive_read_free(a_in);   // also do cleanup!
    }
}

auto enable_write_filter(archive* aw, ArchiveType type) -> bool {
    switch (type) {
        case ArchiveType::Tar:
            return true;  // no compression filter
        case ArchiveType::TarGz:
            return (archive_write_add_filter_gzip(aw) == ARCHIVE_OK);
        case ArchiveType::TarBz2:
            return (archive_write_add_filter_bzip2(aw) == ARCHIVE_OK);
        case ArchiveType::TarXz:
            return (archive_write_add_filter_xz(aw) == ARCHIVE_OK);
        case ArchiveType::TarLz:
            return (archive_write_add_filter_lzip(aw) == ARCHIVE_OK);
        case ArchiveType::TarLzma:
            return (archive_write_add_filter_lzma(aw) == ARCHIVE_OK);
        default:
            return false;
    }
}

auto enable_read_filter(archive* ar, ArchiveType type) -> bool {
    switch (type) {
        case ArchiveType::Tar:
            return true;  // no outside compression filter
        case ArchiveType::TarGz:
            return (archive_read_support_filter_gzip(ar) == ARCHIVE_OK);
        case ArchiveType::TarBz2:
            return (archive_read_support_filter_bzip2(ar) == ARCHIVE_OK);
        case ArchiveType::TarXz:
            return (archive_read_support_filter_xz(ar) == ARCHIVE_OK);
        case ArchiveType::TarLz:
            return (archive_read_support_filter_lzip(ar) == ARCHIVE_OK);
        case ArchiveType::TarLzma:
            return (archive_read_support_filter_lzma(ar) == ARCHIVE_OK);
        case ArchiveType::TarAuto:
            return (archive_read_support_filter_all(ar) == ARCHIVE_OK);
        default:
            return false;
    }
}

}  // namespace

auto ArchiveOps::WriteEntry(archive_entry* entry, archive* aw)
    -> std::optional<std::string> {
    std::filesystem::path entry_path{archive_entry_sourcepath(entry)};
    // only write to archive if entry is file
    if (FileSystemManager::IsFile(entry_path)) {
        auto content = FileSystemManager::ReadFile(entry_path);
        if (not content) {
            return "ArchiveOps: failed to open file entry while creating "
                   "archive";
        }
        if (not content->empty()) {
            auto content_size = content->size();
            archive_write_data(aw, content->c_str(), content_size);
        }
    }
    return std::nullopt;
}

auto ArchiveOps::CopyData(archive* ar, archive* aw)
    -> std::optional<std::string> {
    int r{};
    const void* buff{nullptr};
    size_t size{};
    la_int64_t offset{};

    while (true) {
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) {
            return std::nullopt;  // success!
        }
        if (r != ARCHIVE_OK) {
            return std::string("ArchiveOps: ") +
                   std::string(archive_error_string(ar));
        }
        if (archive_write_data_block(aw, buff, size, offset) != ARCHIVE_OK) {
            return std::string("ArchiveOps: ") +
                   std::string(archive_error_string(aw));
        }
    }
    return std::nullopt;  // success!
}

auto ArchiveOps::EnableWriteFormats(archive* aw, ArchiveType type)
    -> std::optional<std::string> {
    switch (type) {
        case ArchiveType::Zip: {
            if (archive_write_set_format_zip(aw) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(aw));
            }
        } break;
        case ArchiveType::_7Zip: {
            if (archive_write_set_format_7zip(aw) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(aw));
            }
        } break;
        case ArchiveType::Tar:
        case ArchiveType::TarGz:
        case ArchiveType::TarBz2:
        case ArchiveType::TarXz:
        case ArchiveType::TarLz:
        case ArchiveType::TarLzma: {
            if ((archive_write_set_format_pax_restricted(aw) != ARCHIVE_OK) or
                not enable_write_filter(aw, type)) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(aw));
            }
        } break;
        case ArchiveType::TarAuto:
            return std::string(
                "ArchiveOps: Writing a tarball-type archive must be explicit!");
    }
    return std::nullopt;  // success!
}

auto ArchiveOps::EnableReadFormats(archive* ar, ArchiveType type)
    -> std::optional<std::string> {
    switch (type) {
        case ArchiveType::Zip: {
            if (archive_read_support_format_zip(ar) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(ar));
            }
        } break;
        case ArchiveType::_7Zip: {
            if (archive_read_support_format_7zip(ar) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(ar));
            }
        } break;
        case ArchiveType::TarAuto:
        case ArchiveType::Tar:
        case ArchiveType::TarGz:
        case ArchiveType::TarBz2:
        case ArchiveType::TarXz:
        case ArchiveType::TarLz:
        case ArchiveType::TarLzma: {
            if ((archive_read_support_format_tar(ar) != ARCHIVE_OK) or
                not enable_read_filter(ar, type)) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(ar));
            }
        } break;
    }
    return std::nullopt;  // success!
}

auto ArchiveOps::CreateArchive(ArchiveType type,
                               std::string const& name,
                               std::filesystem::path const& source) noexcept
    -> std::optional<std::string> {
    return CreateArchive(type, name, source, std::filesystem::path("."));
}

auto ArchiveOps::CreateArchive(ArchiveType type,
                               std::string const& name,
                               std::filesystem::path const& source,
                               std::filesystem::path const& destDir) noexcept
    -> std::optional<std::string> {
    try {
        // make sure paths will be relative wrt current dir
        auto rel_source = std::filesystem::relative(source);

        std::unique_ptr<archive, decltype(&archive_write_closer)> a_out{
            archive_write_new(), archive_write_closer};
        if (a_out == nullptr) {
            return std::string("ArchiveOps: archive_write_new failed");
        }
        // enable the correct format for archive type
        auto res = EnableWriteFormats(a_out.get(), type);
        if (res != std::nullopt) {
            return *res;
        }
        // open archive to write
        if (not FileSystemManager::CreateDirectory(destDir)) {
            return std::string(
                       "ArchiveOps: could not create destination directory ") +
                   destDir.string();
        }
        if (archive_write_open_filename(
                a_out.get(), (destDir / std::filesystem::path(name)).c_str()) !=
            ARCHIVE_OK) {
            return std::string("ArchiveOps: ") +
                   std::string(archive_error_string(a_out.get()));
        }
        // open source
        std::unique_ptr<archive, decltype(&archive_read_closer)> disk{
            archive_read_disk_new(), archive_read_closer};
        if (disk == nullptr) {
            return std::string("ArchiveOps: archive_read_disk_new failed");
        }
        archive_read_disk_set_standard_lookup(disk.get());
        if (archive_read_disk_open(disk.get(), rel_source.c_str()) !=
            ARCHIVE_OK) {
            return std::string("ArchiveOps: ") +
                   std::string(archive_error_string(disk.get()));
        }
        // create archive
        while (true) {
            std::unique_ptr<archive_entry, decltype(&archive_entry_cleanup)>
                entry{archive_entry_new(), archive_entry_cleanup};
            if (entry == nullptr) {
                return std::string("ArchiveOps: archive_entry_new failed");
            }
            int r = archive_read_next_header2(disk.get(), entry.get());
            if (r == ARCHIVE_EOF) {
                return std::nullopt;  // nothing left to archive; success!
            }
            if (r != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(disk.get()));
            }
            // if entry is a directory, make sure we descend into all its
            // children
            archive_read_disk_descend(disk.get());
            // get info on current entry
            if (archive_write_header(a_out.get(), entry.get()) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(a_out.get()));
            }
            // write entry into archive
            auto res = WriteEntry(entry.get(), a_out.get());
            if (res != std::nullopt) {
                return *res;
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "archive create failed with:\n{}", ex.what());
        return std::nullopt;
    }
}

auto ArchiveOps::ExtractArchive(ArchiveType type,
                                std::filesystem::path const& source) noexcept
    -> std::optional<std::string> {
    return ExtractArchive(type, source, std::filesystem::path("."));
}

auto ArchiveOps::ExtractArchive(ArchiveType type,
                                std::filesystem::path const& source,
                                std::filesystem::path const& destDir) noexcept
    -> std::optional<std::string> {
    try {
        std::unique_ptr<archive, decltype(&archive_read_closer)> a_in{
            archive_read_new(), archive_read_closer};
        if (a_in == nullptr) {
            return std::string("ArchiveOps: archive_read_new failed");
        }
        // enable support for known formats
        auto res = EnableReadFormats(a_in.get(), type);
        if (res != std::nullopt) {
            return *res;
        }
        // open archive for reading
        if (archive_read_open_filename(
                a_in.get(), source.c_str(), kArchiveBlockSize) != ARCHIVE_OK) {
            return std::string("ArchiveOps: ") +
                   std::string(archive_error_string(a_in.get()));
        }
        // set up writer to disk
        std::unique_ptr<archive, decltype(&archive_write_closer)> disk{
            archive_write_disk_new(), archive_write_closer};
        if (disk == nullptr) {
            return std::string("ArchiveOps: archive_write_disk_new failed");
        }
        // Select which attributes we want to restore.
        uint flags = ARCHIVE_EXTRACT_TIME;
        flags |= static_cast<uint>(ARCHIVE_EXTRACT_PERM);
        flags |= static_cast<uint>(ARCHIVE_EXTRACT_FFLAGS);
        archive_write_disk_set_options(disk.get(), static_cast<int>(flags));
        archive_write_disk_set_standard_lookup(disk.get());
        // make sure destination directory exists
        if (not FileSystemManager::CreateDirectory(destDir)) {
            return std::string(
                       "ArchiveOps: could not create destination directory ") +
                   destDir.string();
        }
        // extract the archive
        archive_entry* entry{nullptr};
        while (true) {
            int r = archive_read_next_header(a_in.get(), &entry);
            if (r == ARCHIVE_EOF) {
                return std::nullopt;  // nothing left to extract; success!
            }
            if (r != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(a_in.get()));
            }
            // set correct destination path
            auto new_entry_path =
                destDir / std::filesystem::path(archive_entry_pathname(entry));
            archive_entry_set_pathname(entry, new_entry_path.c_str());
            if (archive_write_header(disk.get(), entry) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(disk.get()));
            }
            // write to disk if file
            if (archive_entry_size(entry) > 0) {
                auto res = CopyData(a_in.get(), disk.get());
                if (res != std::nullopt) {
                    return *res;
                }
            }
            // finish entry writing
            if (archive_write_finish_entry(disk.get()) != ARCHIVE_OK) {
                return std::string("ArchiveOps: ") +
                       std::string(archive_error_string(disk.get()));
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "archive extract failed with:\n{}", ex.what());
        return std::nullopt;
    }
}
