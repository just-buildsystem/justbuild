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

#include <string>
#include <unordered_map>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/other_tools/utils/archive_ops.hpp"

extern "C" {
#include <archive.h>
#include <archive_entry.h>
}

namespace {

using file_t = std::pair</*content*/ std::string, mode_t>;
using filetree_t = std::unordered_map<std::string, file_t>;

constexpr size_t kBlockSize = 10240;
constexpr int kFilePerm = 0644;
constexpr int kDirectoryPerm = 0755;

auto const kExpected = filetree_t{{"foo", {"foo", AE_IFREG}},
                                  {"bar/", {"", AE_IFDIR}},
                                  {"bar/baz", {"baz", AE_IFREG}}};

[[nodiscard]] auto read_archive(archive* a, std::string const& path)
    -> filetree_t {
    filetree_t result{};

    REQUIRE(archive_read_open_filename(a, path.c_str(), kBlockSize) ==
            ARCHIVE_OK);

    archive_entry* entry{};
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        auto size = archive_entry_size(entry);
        auto buf = std::string(static_cast<size_t>(size), '\0');
        REQUIRE(archive_read_data(a, buf.data(), buf.size()) ==
                static_cast<ssize_t>(buf.size()));
        result.emplace(archive_entry_pathname(entry),
                       file_t{buf, archive_entry_filetype(entry)});
    }
    REQUIRE(archive_read_close(a) == ARCHIVE_OK);

    return result;
}

void write_archive(archive* a,
                   std::string const& path,
                   filetree_t const& files) {
    REQUIRE(archive_write_open_filename(a, path.c_str()) == ARCHIVE_OK);

    archive_entry* entry = archive_entry_new();
    for (auto const& [path, file] : files) {
        auto const& [content, type] = file;
        archive_entry_set_pathname(entry, path.c_str());
        archive_entry_set_filetype(entry, type);
        if (type == AE_IFREG) {
            auto buf = std::filesystem::path{path}.filename().string();
            archive_entry_set_perm(entry, kFilePerm);
            archive_entry_set_size(entry, static_cast<int64_t>(buf.size()));
            REQUIRE(archive_write_header(a, entry) == ARCHIVE_OK);
            REQUIRE(archive_write_data(a, buf.data(), buf.size()) ==
                    static_cast<ssize_t>(buf.size()));
        }
        else {
            archive_entry_set_perm(entry, kDirectoryPerm);
            archive_entry_set_size(entry, 0);
            REQUIRE(archive_write_header(a, entry) == ARCHIVE_OK);
        }
        entry = archive_entry_clear(entry);
    }
    archive_entry_free(entry);
    REQUIRE(archive_write_close(a) == ARCHIVE_OK);
}

void extract_archive(std::string const& path) {
    auto* a = archive_read_new();
    REQUIRE(a != nullptr);
    REQUIRE(archive_read_support_format_tar(a) == ARCHIVE_OK);
    REQUIRE(archive_read_support_format_zip(a) == ARCHIVE_OK);
    REQUIRE(archive_read_support_filter_gzip(a) == ARCHIVE_OK);
    REQUIRE(archive_read_support_filter_bzip2(a) == ARCHIVE_OK);
    REQUIRE(archive_read_open_filename(a, path.c_str(), kBlockSize) ==
            ARCHIVE_OK);

    auto* out = archive_write_disk_new();
    REQUIRE(out != nullptr);
    archive_entry* entry{};
    int r{};
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        REQUIRE(archive_write_header(out, entry) == ARCHIVE_OK);
        if (archive_entry_size(entry) > 0) {
            void const* buf{};
            size_t size{};
            int64_t offset{};
            int r2{};
            while ((r2 = archive_read_data_block(a, &buf, &size, &offset)) ==
                   ARCHIVE_OK) {
                REQUIRE(archive_write_data_block(out, buf, size, offset) ==
                        ARCHIVE_OK);
            }
            REQUIRE(r2 == ARCHIVE_EOF);
            REQUIRE(archive_write_finish_entry(out) == ARCHIVE_OK);
        }
    }
    REQUIRE(r == ARCHIVE_EOF);
    REQUIRE(archive_read_close(a) == ARCHIVE_OK);
    REQUIRE(archive_read_free(a) == ARCHIVE_OK);
    REQUIRE(archive_write_close(out) == ARCHIVE_OK);
    REQUIRE(archive_write_free(out) == ARCHIVE_OK);
}

void compare_extracted(
    std::filesystem::path const& extract_dir = ".") noexcept {
    for (auto const& [path, file] : kExpected) {
        auto const& [content, type] = file;
        switch (type) {
            case AE_IFREG: {
                REQUIRE(FileSystemManager::IsFile(extract_dir / path));
                auto data = FileSystemManager::ReadFile(extract_dir / path);
                REQUIRE(data);
                CHECK(*data == content);
            } break;
            case AE_IFDIR:
                CHECK(FileSystemManager::IsDirectory(extract_dir / path));
                break;
            default:
                CHECK(false);
        }
    }
}

void create_files(std::filesystem::path const& destDir = ".") noexcept {
    for (auto const& [path, file] : kExpected) {
        auto const& [content, type] = file;
        switch (type) {
            case AE_IFREG: {
                CHECK(FileSystemManager::WriteFile(content, destDir / path));
            } break;
            case AE_IFDIR:
                CHECK(FileSystemManager::CreateDirectory(destDir / path));
                break;
            default:
                CHECK(false);
        }
    }
}

}  // namespace

TEST_CASE("Archive read context", "[archive_context]") {
    auto* a = archive_read_new();
    REQUIRE(a != nullptr);
    CHECK(archive_read_free(a) == ARCHIVE_OK);
}

TEST_CASE("Archive write context", "[archive_context]") {
    auto* a = archive_write_new();
    REQUIRE(a != nullptr);
    CHECK(archive_write_free(a) == ARCHIVE_OK);
}

TEST_CASE("Archive write disk context", "[archive_context]") {
    auto* a = archive_write_disk_new();
    REQUIRE(a != nullptr);
    CHECK(archive_read_free(a) == ARCHIVE_OK);
}

TEST_CASE("Read-write tar", "[archive_read_write]") {
    std::string test_dir{"test_tar"};
    std::string filename{"test.tar"};

    REQUIRE(FileSystemManager::RemoveDirectory(test_dir, /*recursively=*/true));
    REQUIRE(FileSystemManager::CreateDirectory(test_dir));
    auto anchor = FileSystemManager::ChangeDirectory(test_dir);

    SECTION("Write tar") {
        auto* out = archive_write_new();
        REQUIRE(out != nullptr);
        REQUIRE(archive_write_set_format_pax_restricted(out) == ARCHIVE_OK);
        write_archive(out, filename, kExpected);
        REQUIRE(archive_write_free(out) == ARCHIVE_OK);

        SECTION("Read tar") {
            auto* in = archive_read_new();
            REQUIRE(in != nullptr);
            REQUIRE(archive_read_support_format_tar(in) == ARCHIVE_OK);
            CHECK(read_archive(in, filename) == kExpected);
            REQUIRE(archive_read_free(in) == ARCHIVE_OK);
        }

        SECTION("Extract tar to disk") {
            extract_archive(filename);
            compare_extracted();
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar")) {
            SECTION("Extract via system tar") {
                REQUIRE(system(("/usr/bin/tar xf " + filename).c_str()) == 0);
                compare_extracted();
            }
        }
    }
}

TEST_CASE("Read-write tar.gz", "[archive_read_write]") {
    std::string test_dir{"test_tar_gz"};
    std::string filename{"test.tar.gz"};

    REQUIRE(FileSystemManager::RemoveDirectory(test_dir, /*recursively=*/true));
    REQUIRE(FileSystemManager::CreateDirectory(test_dir));
    auto anchor = FileSystemManager::ChangeDirectory(test_dir);

    SECTION("Write tar.gz") {
        auto* out = archive_write_new();
        REQUIRE(out != nullptr);
        REQUIRE(archive_write_set_format_pax_restricted(out) == ARCHIVE_OK);
        REQUIRE(archive_write_add_filter_gzip(out) == ARCHIVE_OK);
        write_archive(out, filename, kExpected);
        REQUIRE(archive_write_free(out) == ARCHIVE_OK);

        SECTION("Read tar.gz") {
            auto* in = archive_read_new();
            REQUIRE(in != nullptr);
            REQUIRE(archive_read_support_format_tar(in) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_gzip(in) == ARCHIVE_OK);
            CHECK(read_archive(in, filename) == kExpected);
            REQUIRE(archive_read_free(in) == ARCHIVE_OK);
        }

        SECTION("Extract tar.gz to disk") {
            extract_archive(filename);
            compare_extracted();
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar") and
            FileSystemManager::IsExecutable("/usr/bin/gzip")) {
            SECTION("Extract via system tar and gzip") {
                REQUIRE(system(("/usr/bin/tar xzf " + filename).c_str()) == 0);
                compare_extracted();
            }
        }
    }
}

TEST_CASE("Read-write tar.bz2", "[archive_read_write]") {
    std::string test_dir{"test_tar_bz2"};
    std::string filename{"test.tar.bz2"};

    REQUIRE(FileSystemManager::RemoveDirectory(test_dir, /*recursively=*/true));
    REQUIRE(FileSystemManager::CreateDirectory(test_dir));
    auto anchor = FileSystemManager::ChangeDirectory(test_dir);

    SECTION("Write tar.bz2") {
        auto* out = archive_write_new();
        REQUIRE(out != nullptr);
        REQUIRE(archive_write_set_format_pax_restricted(out) == ARCHIVE_OK);
        REQUIRE(archive_write_add_filter_bzip2(out) == ARCHIVE_OK);
        write_archive(out, filename, kExpected);
        REQUIRE(archive_write_free(out) == ARCHIVE_OK);

        SECTION("Read tar.bz2") {
            auto* in = archive_read_new();
            REQUIRE(in != nullptr);
            REQUIRE(archive_read_support_format_tar(in) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_bzip2(in) == ARCHIVE_OK);
            CHECK(read_archive(in, filename) == kExpected);
            REQUIRE(archive_read_free(in) == ARCHIVE_OK);
        }

        SECTION("Extract tar.bz2 to disk") {
            extract_archive(filename);
            compare_extracted();
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar") and
            FileSystemManager::IsExecutable("/usr/bin/bzip2")) {
            SECTION("Extract via system tar and bzip2") {
                REQUIRE(system(("/usr/bin/tar xjf " + filename).c_str()) == 0);
                compare_extracted();
            }
        }
    }
}

TEST_CASE("Read-write zip", "[archive_read_write]") {
    std::string test_dir{"test_zip"};
    std::string filename{"test.zip"};

    REQUIRE(FileSystemManager::RemoveDirectory(test_dir, /*recursively=*/true));
    REQUIRE(FileSystemManager::CreateDirectory(test_dir));
    auto anchor = FileSystemManager::ChangeDirectory(test_dir);

    SECTION("Write zip") {
        auto* out = archive_write_new();
        REQUIRE(out != nullptr);
        REQUIRE(archive_write_set_format_zip(out) == ARCHIVE_OK);
        write_archive(out, filename, kExpected);
        REQUIRE(archive_write_free(out) == ARCHIVE_OK);

        SECTION("Read zip") {
            auto* in = archive_read_new();
            REQUIRE(in != nullptr);
            REQUIRE(archive_read_support_format_zip(in) == ARCHIVE_OK);
            CHECK(read_archive(in, filename) == kExpected);
            REQUIRE(archive_read_free(in) == ARCHIVE_OK);
        }

        SECTION("Extract zip to disk") {
            extract_archive(filename);
            compare_extracted();
        }

        if (FileSystemManager::IsExecutable("/usr/bin/unzip")) {
            SECTION("Extract via system unzip") {
                REQUIRE(system(("/usr/bin/unzip " + filename).c_str()) == 0);
                compare_extracted();
            }
        }
    }
}

TEST_CASE("ArchiveOps", "[archive_ops]") {
    std::optional<std::string> res{std::nullopt};

    SECTION("Write tar") {
        std::string test_dir_tar{"ops_test_tar"};
        std::string filename_tar{"test.tar"};

        REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar,
                                                   /*recursively=*/true));
        REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar));

        create_files(test_dir_tar);

        res = ArchiveOps::CreateArchive(
            ArchiveType::kArchiveTypeTar, filename_tar, test_dir_tar, ".");
        if (res != std::nullopt) {
            FAIL(*res);
        }

        SECTION("Extract tar to disk") {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar));
            res = ArchiveOps::ExtractArchive(
                ArchiveType::kArchiveTypeTar, filename_tar, ".");
            if (res != std::nullopt) {
                FAIL(*res);
            }
            compare_extracted(test_dir_tar);
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar")) {
            SECTION("Extract via system tar") {
                REQUIRE(
                    FileSystemManager::RemoveDirectory(test_dir_tar,
                                                       /*recursively=*/true));
                REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar));

                REQUIRE(system(("/usr/bin/tar xf " + filename_tar).c_str()) ==
                        0);
                compare_extracted(test_dir_tar);
            }
        }
    }

    SECTION("Write tar.gz") {
        std::string test_dir_tar_gz{"ops_test_tar_gz"};
        std::string filename_tar_gz{"test.tar.gz"};

        REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_gz,
                                                   /*recursively=*/true));
        REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_gz));

        create_files(test_dir_tar_gz);

        res = ArchiveOps::CreateArchive(ArchiveType::kArchiveTypeTarGz,
                                        filename_tar_gz,
                                        test_dir_tar_gz,
                                        ".");
        if (res != std::nullopt) {
            FAIL(*res);
        }

        SECTION("Extract tar.gz to disk") {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_gz,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_gz));
            res = ArchiveOps::ExtractArchive(
                ArchiveType::kArchiveTypeTarGz, filename_tar_gz, ".");
            if (res != std::nullopt) {
                FAIL(*res);
            }
            compare_extracted(test_dir_tar_gz);
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar") and
            FileSystemManager::IsExecutable("/usr/bin/gzip")) {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_gz,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_gz));

            SECTION("Extract via system tar and gzip") {
                REQUIRE(
                    system(("/usr/bin/tar xzf " + filename_tar_gz).c_str()) ==
                    0);
                compare_extracted(test_dir_tar_gz);
            }
        }
    }

    SECTION("Write tar.bz2") {
        std::string test_dir_tar_bz2{"ops_test_tar_bz2"};
        std::string filename_tar_bz2{"test.tar.bz2"};

        REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_bz2,
                                                   /*recursively=*/true));
        REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_bz2));

        create_files(test_dir_tar_bz2);

        res = ArchiveOps::CreateArchive(ArchiveType::kArchiveTypeTarBz2,
                                        filename_tar_bz2,
                                        test_dir_tar_bz2,
                                        ".");
        if (res != std::nullopt) {
            FAIL(*res);
        }

        SECTION("Extract tar.bz2 to disk") {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_bz2,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_bz2));
            res = ArchiveOps::ExtractArchive(
                ArchiveType::kArchiveTypeTarBz2, filename_tar_bz2, ".");
            if (res != std::nullopt) {
                FAIL(*res);
            }
            compare_extracted(test_dir_tar_bz2);
        }

        if (FileSystemManager::IsExecutable("/usr/bin/tar") and
            FileSystemManager::IsExecutable("/usr/bin/bzip2")) {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_tar_bz2,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_tar_bz2));

            SECTION("Extract via system tar and bzip2") {
                REQUIRE(
                    system(("/usr/bin/tar xjf " + filename_tar_bz2).c_str()) ==
                    0);
                compare_extracted(test_dir_tar_bz2);
            }
        }
    }

    SECTION("Write zip") {
        std::string test_dir_zip{"ops_test_zip"};
        std::string filename_zip{"test.zip"};

        REQUIRE(FileSystemManager::RemoveDirectory(test_dir_zip,
                                                   /*recursively=*/true));
        REQUIRE(FileSystemManager::CreateDirectory(test_dir_zip));

        create_files(test_dir_zip);

        res = ArchiveOps::CreateArchive(
            ArchiveType::kArchiveTypeZip, filename_zip, test_dir_zip, ".");
        if (res != std::nullopt) {
            FAIL(*res);
        }

        SECTION("Extract zip to disk") {
            REQUIRE(FileSystemManager::RemoveDirectory(test_dir_zip,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(test_dir_zip));
            res = ArchiveOps::ExtractArchive(
                ArchiveType::kArchiveTypeZip, filename_zip, ".");
            if (res != std::nullopt) {
                FAIL(*res);
            }
            compare_extracted(test_dir_zip);
        }

        if (FileSystemManager::IsExecutable("/usr/bin/unzip")) {
            SECTION("Extract via system unzip") {
                REQUIRE(
                    FileSystemManager::RemoveDirectory(test_dir_zip,
                                                       /*recursively=*/true));
                REQUIRE(FileSystemManager::CreateDirectory(test_dir_zip));

                REQUIRE(system(("/usr/bin/unzip " + filename_zip).c_str()) ==
                        0);
                compare_extracted(test_dir_zip);
            }
        }
    }
}
