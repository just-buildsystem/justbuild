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
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators_all.hpp"
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

struct archive_test_info_t {
    std::string test_name;
    ArchiveType type;
    std::string test_dir;
    std::string filename;
    std::vector<std::string> tools;
    std::string cmd;
};

std::vector<archive_test_info_t> const kTestScenarios = {
    {.test_name = "tar",
     .type = ArchiveType::Tar,
     .test_dir = "test_tar",
     .filename = "test.tar",
     .tools = {"tar"},
     .cmd = "/usr/bin/tar xf"},
    {.test_name = "tar.gz",
     .type = ArchiveType::TarGz,
     .test_dir = "test_tar_gz",
     .filename = "test.tar.gz",
     .tools = {"tar", "gzip"},
     .cmd = "/usr/bin/tar xzf"},
    {.test_name = "tar.bz2",
     .type = ArchiveType::TarBz2,
     .test_dir = "test_tar_bz2",
     .filename = "test.tar.bz2",
     .tools = {"tar", "bzip2"},
     .cmd = "/usr/bin/tar xjf"},
    {.test_name = "tar.xz",
     .type = ArchiveType::TarXz,
     .test_dir = "test_tar_xz",
     .filename = "test.tar.xz",
     .tools = {"tar", "xz"},
     .cmd = "/usr/bin/tar xJf"},
    {.test_name = "tar.lz",
     .type = ArchiveType::TarLz,
     .test_dir = "test_tar_lz",
     .filename = "test.tar.lz",
     .tools = {"tar", "lzip"},
     .cmd = "/usr/bin/tar --lzip -x -f"},
    {.test_name = "tar.lzma",
     .type = ArchiveType::TarLzma,
     .test_dir = "test_tar_lzma",
     .filename = "test.tar.lzma",
     .tools = {"tar", "lzma"},
     .cmd = "/usr/bin/tar --lzma -x -f"},
    {.test_name = "zip",
     .type = ArchiveType::Zip,
     .test_dir = "test_zip",
     .filename = "test.zip",
     .tools = {"unzip"},
     .cmd = "/usr/bin/unzip"}};

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
    REQUIRE(archive_read_support_filter_xz(a) == ARCHIVE_OK);
    REQUIRE(archive_read_support_filter_lzip(a) == ARCHIVE_OK);
    REQUIRE(archive_read_support_filter_lzma(a) == ARCHIVE_OK);
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

void enable_write_format_and_filter(archive* aw, ArchiveType type) {
    switch (type) {
        case ArchiveType::Zip: {
            REQUIRE(archive_write_set_format_zip(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::Tar: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarGz: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_gzip(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarBz2: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_bzip2(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarXz: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_xz(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarLz: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_lzip(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarLzma: {
            REQUIRE(archive_write_set_format_pax_restricted(aw) == ARCHIVE_OK);
            REQUIRE(archive_write_add_filter_lzma(aw) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarAuto:
            return;  // unused
    }
}

void enable_read_format_and_filter(archive* ar, ArchiveType type) {
    switch (type) {
        case ArchiveType::Zip: {
            REQUIRE(archive_read_support_format_zip(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::Tar: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarGz: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_gzip(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarBz2: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_bzip2(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarXz: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_xz(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarLz: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_lzip(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarLzma: {
            REQUIRE(archive_read_support_format_tar(ar) == ARCHIVE_OK);
            REQUIRE(archive_read_support_filter_lzma(ar) == ARCHIVE_OK);
        } break;
        case ArchiveType::TarAuto:
            return;  // unused
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

TEST_CASE("Read-write archives", "[archive_read_write]") {
    // get the scenario
    auto test_index =
        GENERATE(Catch::Generators::range<size_t>(0, kTestScenarios.size()));
    auto const& scenario = kTestScenarios[test_index];

    // perform the test
    REQUIRE(FileSystemManager::RemoveDirectory(scenario.test_dir,
                                               /*recursively=*/true));
    REQUIRE(FileSystemManager::CreateDirectory(scenario.test_dir));
    auto anchor = FileSystemManager::ChangeDirectory(scenario.test_dir);

    SECTION(std::string("Write ") + scenario.test_name) {
        auto* out = archive_write_new();
        REQUIRE(out != nullptr);
        enable_write_format_and_filter(out, scenario.type);
        write_archive(out, scenario.filename, kExpected);
        REQUIRE(archive_write_free(out) == ARCHIVE_OK);

        SECTION(std::string("Read ") + scenario.test_name) {
            auto* in = archive_read_new();
            REQUIRE(in != nullptr);
            enable_read_format_and_filter(in, scenario.type);
            CHECK(read_archive(in, scenario.filename) == kExpected);
            REQUIRE(archive_read_free(in) == ARCHIVE_OK);
        }

        SECTION(std::string("Extract ") + scenario.test_name + " to disk") {
            extract_archive(scenario.filename);
            compare_extracted();
        }

        bool tools_exist{true};
        for (auto const& tool : scenario.tools) {
            tools_exist &= FileSystemManager::IsExecutable(
                std::string("/usr/bin/") + tool);
        }

        if (tools_exist) {
            SECTION("Extract via system tools") {
                REQUIRE(
                    system((scenario.cmd + " " + scenario.filename).c_str()) ==
                    0);
                compare_extracted();
            }
        }
    }
}

TEST_CASE("ArchiveOps", "[archive_ops]") {
    // get the scenario
    auto test_index =
        GENERATE(Catch::Generators::range<size_t>(0, kTestScenarios.size()));
    auto const& scenario = kTestScenarios[test_index];

    // perform the test
    std::optional<std::string> res{std::nullopt};

    SECTION(std::string("Write ") + scenario.test_name) {
        REQUIRE(FileSystemManager::RemoveDirectory(scenario.test_dir,
                                                   /*recursively=*/true));
        REQUIRE(FileSystemManager::CreateDirectory(scenario.test_dir));

        create_files(scenario.test_dir);

        res = ArchiveOps::CreateArchive(
            scenario.type, scenario.filename, scenario.test_dir, ".");
        if (res != std::nullopt) {
            FAIL(*res);
        }

        SECTION(std::string("Extract ") + scenario.test_name + " to disk") {
            REQUIRE(FileSystemManager::RemoveDirectory(scenario.test_dir,
                                                       /*recursively=*/true));
            REQUIRE(FileSystemManager::CreateDirectory(scenario.test_dir));
            res = ArchiveOps::ExtractArchive(
                scenario.type, scenario.filename, ".");
            if (res != std::nullopt) {
                FAIL(*res);
            }
            compare_extracted(scenario.test_dir);
        }

        bool tools_exist{true};
        for (auto const& tool : scenario.tools) {
            tools_exist &= FileSystemManager::IsExecutable(
                std::string("/usr/bin/") + tool);
        }
        if (tools_exist) {
            SECTION("Extract via system tools") {
                REQUIRE(
                    FileSystemManager::RemoveDirectory(scenario.test_dir,
                                                       /*recursively=*/true));
                REQUIRE(FileSystemManager::CreateDirectory(scenario.test_dir));

                REQUIRE(
                    system((scenario.cmd + " " + scenario.filename).c_str()) ==
                    0);
                compare_extracted(scenario.test_dir);
            }
        }
    }
}
