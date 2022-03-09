#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "catch2/catch.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

class CopyFileFixture {
  public:
    CopyFileFixture() noexcept {
        REQUIRE(FileSystemManager::CreateDirectory(to_.parent_path()));
    }
    CopyFileFixture(CopyFileFixture const&) = delete;
    CopyFileFixture(CopyFileFixture&&) = delete;
    ~CopyFileFixture() noexcept { CHECK(std::filesystem::remove(to_)); }
    auto operator=(CopyFileFixture const&) -> CopyFileFixture& = delete;
    auto operator=(CopyFileFixture &&) -> CopyFileFixture& = delete;

    std::filesystem::path const from_{
        "test/buildtool/file_system/data/example_file"};
    std::filesystem::path const to_{"./tmp-CopyFile/copied_file"};
};

class WriteFileFixture {
  public:
    WriteFileFixture() noexcept {
        REQUIRE(FileSystemManager::CreateDirectory(root_dir_));
    }
    WriteFileFixture(WriteFileFixture const&) = delete;
    WriteFileFixture(WriteFileFixture&&) = delete;
    ~WriteFileFixture() noexcept { CHECK(std::filesystem::remove(file_path_)); }
    auto operator=(WriteFileFixture const&) -> WriteFileFixture& = delete;
    auto operator=(WriteFileFixture &&) -> WriteFileFixture& = delete;

    std::filesystem::path const relative_path_parent_{
        GENERATE(as<std::filesystem::path>{},
                 ".",
                 "level0",
                 "level0/level1",
                 "a/b/c/d",
                 "./a/../e")};
    std::filesystem::path const root_dir_{"./tmp-RemoveFile"};
    std::filesystem::path const file_path_{root_dir_ / relative_path_parent_ /
                                           "file"};
};

namespace {

namespace fs = std::filesystem;

constexpr auto kFilePerms =
    fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;

constexpr auto kExecPerms =
    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;

auto HasFilePermissions(fs::path const& path) noexcept -> bool {
    try {
        return fs::status(path).permissions() == kFilePerms;
    } catch (...) {
        return false;
    }
}

auto HasExecutablePermissions(fs::path const& path) noexcept -> bool {
    try {
        return fs::status(path).permissions() == (kFilePerms | kExecPerms);
    } catch (...) {
        return false;
    }
}

auto HasEpochTime(fs::path const& path) noexcept -> bool {
    try {
        return fs::last_write_time(path) ==
               System::GetPosixEpoch<std::chrono::file_clock>();
    } catch (...) {
        return false;
    }
}

}  // namespace

TEST_CASE("CreateDirectory", "[file_system]") {
    auto const dir = GENERATE(as<std::filesystem::path>{},
                              "level0",
                              "level0/level1",
                              "a/b/c/d",
                              "./a/../e");
    CHECK(FileSystemManager::CreateDirectory(dir));
    CHECK(std::filesystem::exists(dir));
    CHECK(std::filesystem::is_directory(dir));

    // If we have created the directory already, CreateDirectory() returns true
    // and the state of things doesn't change
    CHECK(FileSystemManager::CreateDirectory(dir));
    CHECK(std::filesystem::exists(dir));
    CHECK(std::filesystem::is_directory(dir));
}

TEST_CASE("IsFile", "[file_system]") {
    CHECK(FileSystemManager::IsFile(
        "test/buildtool/file_system/data/example_file"));
    CHECK(FileSystemManager::IsFile(
        "test/buildtool/file_system/data/empty_executable"));
    CHECK_FALSE(FileSystemManager::IsFile("test/buildtool/file_system/data/"));
}

TEST_CASE("IsExecutable", "[file_system]") {
    CHECK(FileSystemManager::IsExecutable(
        "test/buildtool/file_system/data/empty_executable"));
    CHECK_FALSE(FileSystemManager::IsExecutable(
        "test/buildtool/file_system/data/example_file"));
    CHECK_FALSE(
        FileSystemManager::IsExecutable("test/buildtool/file_system/data/"));
}

TEST_CASE("Type", "[file_system]") {
    auto const type_file =
        FileSystemManager::Type("test/buildtool/file_system/data/example_file");
    REQUIRE(type_file);
    CHECK(*type_file == ObjectType::File);

    auto const type_exec = FileSystemManager::Type(
        "test/buildtool/file_system/data/empty_executable");
    REQUIRE(type_exec);
    CHECK(*type_exec == ObjectType::Executable);

    auto const type_dir =
        FileSystemManager::Type("test/buildtool/file_system/data/");
    REQUIRE(type_dir);
    CHECK(*type_dir == ObjectType::Tree);
}

TEST_CASE("ChangeDirectory", "[file_system]") {
    auto const starting_dir = FileSystemManager::GetCurrentDirectory();

    auto const new_dir = GENERATE(as<std::filesystem::path>{},
                                  "level0",
                                  "level0/level1",
                                  "a/b/c/d",
                                  "./a/../e");

    REQUIRE(FileSystemManager::CreateDirectory(new_dir));
    {
        auto anchor = FileSystemManager::ChangeDirectory(new_dir);
        CHECK(std::filesystem::equivalent(
            starting_dir / new_dir, FileSystemManager::GetCurrentDirectory()));
    }
    CHECK(starting_dir == FileSystemManager::GetCurrentDirectory());
}

TEST_CASE("ReadFile", "[file_system]") {
    SECTION("Existing file") {
        std::string const expected_content{"test\n"};
        std::filesystem::path file{"./tmp-ReadFile/file"};

        REQUIRE(FileSystemManager::CreateDirectory(file.parent_path()));
        std::ofstream writer{file};
        writer << expected_content;
        writer.close();

        auto const content = FileSystemManager::ReadFile(file);
        CHECK(content.has_value());
        CHECK(content == expected_content);
    }
    SECTION("Non-existing file") {
        std::filesystem::path file{
            "test/buildtool/file_system/data/this_file_does_not_exist"};
        REQUIRE(not std::filesystem::exists(file));

        auto const content = FileSystemManager::ReadFile(file);
        CHECK_FALSE(content.has_value());
    }
}

TEST_CASE_METHOD(CopyFileFixture, "CopyFile", "[file_system]") {
    auto run_test = [&](bool fd_less) {
        // Copy file was successful
        CHECK(FileSystemManager::CopyFile(from_, to_, fd_less));

        // file exists
        CHECK(std::filesystem::exists(to_));
        CHECK(std::filesystem::is_regular_file(to_));

        // Contents are equal
        auto const content_from = FileSystemManager::ReadFile(from_);
        CHECK(content_from.has_value());
        auto const content_to = FileSystemManager::ReadFile(to_);
        CHECK(content_to.has_value());
        CHECK(content_from == content_to);
    };

    SECTION("direct") { run_test(false); }
    SECTION("fd_less") { run_test(true); }
}

TEST_CASE_METHOD(CopyFileFixture, "CopyFileAs", "[file_system]") {
    SECTION("as file") {
        auto run_test = [&]<bool kSetEpochTime = false>(bool fd_less) {
            // Copy as file was successful
            CHECK(FileSystemManager::CopyFileAs<kSetEpochTime>(
                from_, to_, ObjectType::File, fd_less));

            // file exists
            CHECK(std::filesystem::exists(to_));
            CHECK(std::filesystem::is_regular_file(to_));
            CHECK(not FileSystemManager::IsExecutable(to_));

            // Contents are equal
            auto const content_from = FileSystemManager::ReadFile(from_);
            CHECK(content_from.has_value());
            auto const content_to = FileSystemManager::ReadFile(to_);
            CHECK(content_to.has_value());
            CHECK(content_from == content_to);

            // permissions should be 0444
            CHECK(HasFilePermissions(to_));
            if constexpr (kSetEpochTime) {
                CHECK(HasEpochTime(to_));
            }
        };

        SECTION("direct") { run_test(false); }
        SECTION("fd_less") { run_test(true); }
        SECTION("direct with epoch") {
            run_test.template operator()<true>(false);
        }
        SECTION("fd_less with epoch") {
            run_test.template operator()<true>(true);
        }
    }
    SECTION("as executable") {
        auto run_test = [&]<bool kSetEpochTime = false>(bool fd_less) {
            // Copy as file was successful
            CHECK(FileSystemManager::CopyFileAs<kSetEpochTime>(
                from_, to_, ObjectType::Executable, fd_less));

            // file exists
            CHECK(std::filesystem::exists(to_));
            CHECK(std::filesystem::is_regular_file(to_));
            CHECK(FileSystemManager::IsExecutable(to_));

            // Contents are equal
            auto const content_from = FileSystemManager::ReadFile(from_);
            CHECK(content_from.has_value());
            auto const content_to = FileSystemManager::ReadFile(to_);
            CHECK(content_to.has_value());
            CHECK(content_from == content_to);

            // permissions should be 0555
            CHECK(HasExecutablePermissions(to_));
            if constexpr (kSetEpochTime) {
                CHECK(HasEpochTime(to_));
            }
        };

        SECTION("direct") { run_test(false); }
        SECTION("fd_less") { run_test(true); }
        SECTION("direct with epoch") {
            run_test.template operator()<true>(false);
        }
        SECTION("fd_less with epoch") {
            run_test.template operator()<true>(true);
        }
    }
}

TEST_CASE("RemoveFile", "[file_system]") {
    SECTION("Existing file") {
        std::filesystem::path from{
            "test/buildtool/file_system/data/example_file"};

        std::filesystem::path to{"./tmp-RemoveFile/copied_file"};
        REQUIRE(FileSystemManager::CreateDirectory(to.parent_path()));

        CHECK(FileSystemManager::CopyFile(from, to));

        CHECK(std::filesystem::exists(to));

        CHECK(FileSystemManager::RemoveFile(to));

        CHECK(not std::filesystem::exists(to));
    }
    SECTION("Non-existing file") {
        std::filesystem::path file{
            "test/buildtool/file_system/data/"
            "this_file_does_not_exist_neither"};
        CHECK(not std::filesystem::exists(file));
        CHECK(FileSystemManager::RemoveFile(file));  // nothing to delete
    }
    SECTION("Existing but not file") {
        std::filesystem::path dir{"./tmp-RemoveFile/dir"};
        CHECK(FileSystemManager::CreateDirectory(dir));
        CHECK(not FileSystemManager::RemoveFile(dir));
        CHECK(std::filesystem::exists(dir));
    }
}

TEST_CASE_METHOD(WriteFileFixture, "WriteFile", "[file_system]") {
    std::string const content{"This are the contents\nof the file.\n"};

    auto run_test = [&](bool fd_less) {
        CHECK(FileSystemManager::WriteFile(content, file_path_, fd_less));
        CHECK(std::filesystem::exists(file_path_));
        CHECK(std::filesystem::is_directory(file_path_.parent_path()));
        CHECK(std::filesystem::is_regular_file(file_path_));

        auto const written_content = FileSystemManager::ReadFile(file_path_);
        CHECK(written_content.has_value());
        CHECK(written_content == content);
    };

    SECTION("direct") { run_test(false); }
    SECTION("fd-less") { run_test(true); }
}

TEST_CASE_METHOD(WriteFileFixture, "WriteFileAs", "[file_system]") {
    SECTION("as a file") {
        std::string const content{"This are the contents\nof the file.\n"};

        auto run_test = [&]<bool kSetEpochTime = false>(bool fd_less) {
            CHECK(FileSystemManager::WriteFileAs<kSetEpochTime>(
                content, file_path_, ObjectType::File, fd_less));
            CHECK(std::filesystem::exists(file_path_));
            CHECK(std::filesystem::is_directory(file_path_.parent_path()));
            CHECK(std::filesystem::is_regular_file(file_path_));
            CHECK(not FileSystemManager::IsExecutable(file_path_));

            auto const written_content =
                FileSystemManager::ReadFile(file_path_);
            CHECK(written_content.has_value());
            CHECK(written_content == content);

            // permissions should be 0444
            CHECK(HasFilePermissions(file_path_));
            if constexpr (kSetEpochTime) {
                CHECK(HasEpochTime(file_path_));
            }
        };

        SECTION("direct") { run_test(false); }
        SECTION("fd-less") { run_test(true); }
        SECTION("direct with epoch") {
            run_test.template operator()<true>(false);
        }
        SECTION("fd-less with epoch") {
            run_test.template operator()<true>(true);
        }
    }
    SECTION("as an executable") {
        std::string const content{"\n"};

        auto run_test = [&]<bool kSetEpochTime = false>(bool fd_less) {
            CHECK(FileSystemManager::WriteFileAs<kSetEpochTime>(
                content, file_path_, ObjectType::Executable, fd_less));
            CHECK(std::filesystem::exists(file_path_));
            CHECK(std::filesystem::is_directory(file_path_.parent_path()));
            CHECK(std::filesystem::is_regular_file(file_path_));
            CHECK(FileSystemManager::IsExecutable(file_path_));

            auto const written_content =
                FileSystemManager::ReadFile(file_path_);
            CHECK(written_content.has_value());
            CHECK(written_content == content);

            // permissions should be 0555
            CHECK(HasExecutablePermissions(file_path_));
            if constexpr (kSetEpochTime) {
                CHECK(HasEpochTime(file_path_));
            }
        };

        SECTION("direct") { run_test(false); }
        SECTION("fd-less") { run_test(true); }
        SECTION("direct with epoch") {
            run_test.template operator()<true>(false);
        }
        SECTION("fd-less with epoch") {
            run_test.template operator()<true>(true);
        }
    }
}

TEST_CASE("FileSystemManager", "[file_system]") {
    // test file and test file content with newline and null characters
    std::filesystem::path test_file{"test/file"};
    std::filesystem::path copy_file{"test/copy"};
    std::string test_content;
    test_content += "test1";
    test_content += '\n';
    test_content += '\0';
    test_content += "test2";

    CHECK(FileSystemManager::IsRelativePath(test_file));
    CHECK(not FileSystemManager::IsAbsolutePath(test_file));

    // create parent directory
    REQUIRE(FileSystemManager::CreateDirectory(test_file.parent_path()));

    // scope to test RAII "DirectoryAnchor" (should restore CWD on destruction)
    {
        // change directory and obtain DirectoryAnchor
        auto anchor =
            FileSystemManager::ChangeDirectory(test_file.parent_path());

        // assemble file creation command (escape null character)
        std::string create_file_cmd{};
        create_file_cmd += "echo -n \"";
        std::for_each(
            test_content.begin(), test_content.end(), [&](auto const& c) {
                if (c == '\0') {
                    create_file_cmd += std::string{"\\0"};
                }
                else {
                    create_file_cmd += c;
                }
            });
        create_file_cmd += "\" > " + test_file.filename().string();

        // run file creation command
        std::system(create_file_cmd.c_str());

        // check if file exists
        REQUIRE(FileSystemManager::IsFile(test_file.filename()));
    }  // restore CWD to parent path

    // check if file exists with full path
    REQUIRE(FileSystemManager::IsFile(test_file));

    // read file content and compare with input above
    auto const file_content = FileSystemManager::ReadFile(test_file);
    REQUIRE(file_content.has_value());
    CHECK(file_content == test_content);

    // copy file without 'overwrite'
    CHECK(FileSystemManager::CopyFile(test_file,
                                      copy_file,
                                      /*fd_less=*/false,
                                      std::filesystem::copy_options::none));

    // copy file with 'overwrite'
    CHECK(FileSystemManager::CopyFile(copy_file, test_file));

    // remove files and verify removal
    CHECK(FileSystemManager::RemoveFile(test_file));
    CHECK(not FileSystemManager::IsFile(test_file));
    CHECK(FileSystemManager::RemoveFile(copy_file));
    CHECK(not FileSystemManager::IsFile(copy_file));
}

TEST_CASE("CreateFileHardlink", "[file_system]") {
    std::filesystem::path to{"./tmp-CreateFileHardlink/linked_file"};
    REQUIRE(FileSystemManager::CreateDirectory(to.parent_path()));

    SECTION("Existing file") {
        std::filesystem::path from{
            "test/buildtool/file_system/data/example_file"};

        CHECK(FileSystemManager::CreateFileHardlink(from, to));
        CHECK(std::filesystem::exists(to));

        CHECK_FALSE(FileSystemManager::CreateFileHardlink(from, to));
        CHECK(std::filesystem::exists(to));

        CHECK(FileSystemManager::RemoveFile(to));
        CHECK(not std::filesystem::exists(to));
    }
    SECTION("Non-existing file") {
        std::filesystem::path from{
            "test/buildtool/file_system/data/this_file_does_not_exist"};

        CHECK_FALSE(FileSystemManager::CreateFileHardlink(from, to));
        CHECK_FALSE(std::filesystem::exists(to));
    }
    SECTION("Existing but not file") {
        std::filesystem::path from{"./tmp-CreateFileHardlink/dir"};
        CHECK(FileSystemManager::CreateDirectory(from));

        CHECK_FALSE(FileSystemManager::CreateFileHardlink(from, to));
        CHECK_FALSE(std::filesystem::exists(to));
    }
}

TEST_CASE_METHOD(CopyFileFixture, "CreateFileHardlinkAs", "[file_system]") {
    auto set_perm = [&](bool is_executable) {
        auto const content = FileSystemManager::ReadFile(from_);
        REQUIRE(content);
        REQUIRE(FileSystemManager::RemoveFile(from_));
        REQUIRE(FileSystemManager::WriteFileAs(
            *content,
            from_,
            is_executable ? ObjectType::Executable : ObjectType::File));
    };
    auto run_test = [&]<bool kSetEpochTime = false>(bool is_executable) {
        // Hard link creation was successful
        CHECK(FileSystemManager::CreateFileHardlinkAs<kSetEpochTime>(
            from_,
            to_,
            is_executable ? ObjectType::Executable : ObjectType::File));

        // file exists
        CHECK(std::filesystem::exists(to_));
        CHECK(std::filesystem::is_regular_file(to_));
        CHECK(is_executable == FileSystemManager::IsExecutable(to_));

        // permissions should be 0555 or 0444
        CHECK((is_executable ? HasExecutablePermissions(to_)
                             : HasFilePermissions(to_)));
        if constexpr (kSetEpochTime) {
            CHECK(HasEpochTime(to_));
        }
    };
    SECTION("as file") {
        SECTION("from file") {
            set_perm(false);
            run_test(false);
        }
        SECTION("from executable") {
            set_perm(true);
            run_test(false);
        }
    }
    SECTION("as executable") {
        SECTION("from file") {
            set_perm(false);
            run_test(true);
        }
        SECTION("from executable") {
            set_perm(true);
            run_test(true);
        }
    }
    SECTION("as file with epoch") {
        SECTION("from file") {
            set_perm(false);
            run_test.template operator()<true>(false);
        }
        SECTION("from executable") {
            set_perm(true);
            run_test.template operator()<true>(false);
        }
    }
    SECTION("as executable with epoch") {
        SECTION("from file") {
            set_perm(false);
            run_test.template operator()<true>(true);
        }
        SECTION("from executable") {
            set_perm(true);
            run_test.template operator()<true>(true);
        }
    }
}
