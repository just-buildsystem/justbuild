#include <cstdlib>
#include <iostream>

#include "catch2/catch.hpp"
#include "src/buildtool/system/system_command.hpp"

namespace {
[[nodiscard]] auto GetTestDir() -> std::filesystem::path {
    auto* tmp_dir = std::getenv("TEST_TMPDIR");
    if (tmp_dir != nullptr) {
        return tmp_dir;
    }
    return FileSystemManager::GetCurrentDirectory() /
           "test/buildtool/file_system";
}
}  // namespace

TEST_CASE("SystemCommand", "[filesystem]") {
    using Catch::Matchers::Contains;
    using Catch::Matchers::StartsWith;

    std::string name{"ExecutorTest"};
    SystemCommand system{name};

    auto const testdir = GetTestDir();

    SECTION("empty command") {
        auto tmpdir = testdir / "empty";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        auto output = system.Execute(
            {}, {}, FileSystemManager::GetCurrentDirectory(), tmpdir);
        CHECK(not output.has_value());
    }

    SECTION("simple command, no arguments, no env variables") {
        auto tmpdir = testdir / "simple_noargs";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        auto output = system.Execute(
            {"echo"}, {}, FileSystemManager::GetCurrentDirectory(), tmpdir);
        REQUIRE(output.has_value());
        CHECK(output->return_value == 0);
        CHECK(*FileSystemManager::ReadFile(output->stdout_file) == "\n");
        CHECK(FileSystemManager::ReadFile(output->stderr_file)->empty());
    }

    SECTION(
        "simple command, env variables are expanded only when wrapped with "
        "/bin/sh") {
        auto tmpdir = testdir / "simple_env0";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        auto output = system.Execute({"echo", "${MY_MESSAGE}"},
                                     {{"MY_MESSAGE", "hello"}},
                                     FileSystemManager::GetCurrentDirectory(),
                                     tmpdir);
        REQUIRE(output.has_value());
        CHECK(output->return_value == 0);
        CHECK(*FileSystemManager::ReadFile(output->stdout_file) ==
              "${MY_MESSAGE}\n");
        CHECK(FileSystemManager::ReadFile(output->stderr_file)->empty());

        tmpdir = testdir / "simple_env1";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        auto output_wrapped =
            system.Execute({"/bin/sh", "-c", "set -e\necho ${MY_MESSAGE}"},
                           {{"MY_MESSAGE", "hello"}},
                           FileSystemManager::GetCurrentDirectory(),
                           tmpdir);
        REQUIRE(output_wrapped.has_value());
        CHECK(output_wrapped->return_value == 0);
        CHECK(*FileSystemManager::ReadFile(output_wrapped->stdout_file) ==
              "hello\n");
        CHECK(
            FileSystemManager::ReadFile(output_wrapped->stderr_file)->empty());
    }

    SECTION("executable, producing std output, std error and return value") {
        auto tmpdir = testdir / "exe_output";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        auto output = system.Execute(
            {"/bin/sh",
             "-c",
             "set -e\necho this is stdout; echo this is stderr >&2; exit 5"},
            {},
            FileSystemManager::GetCurrentDirectory(),
            tmpdir);
        REQUIRE(output.has_value());
        CHECK(output->return_value == 5);
        CHECK(*FileSystemManager::ReadFile(output->stdout_file) ==
              "this is stdout\n");
        CHECK(*FileSystemManager::ReadFile(output->stderr_file) ==
              "this is stderr\n");
    }

    SECTION(
        "executable dependent on env, producing std output, std error and "
        "return value") {
        auto tmpdir = testdir / "exe_output_from_env";
        REQUIRE(FileSystemManager::CreateDirectoryExclusive(tmpdir));
        std::string const stdout = "this is stdout from env var";
        std::string const stderr = "this is stderr from env var";
        auto output = system.Execute(
            {"/bin/sh",
             "-c",
             "set -e\necho ${MY_STDOUT}; echo ${MY_STDERR} >&2; exit 5"},
            {{"MY_STDOUT", stdout}, {"MY_STDERR", stderr}},
            FileSystemManager::GetCurrentDirectory(),
            tmpdir);
        REQUIRE(output.has_value());
        CHECK(output->return_value == 5);
        CHECK(*FileSystemManager::ReadFile(output->stdout_file) ==
              stdout + '\n');
        CHECK(*FileSystemManager::ReadFile(output->stderr_file) ==
              stderr + '\n');
    }
}
