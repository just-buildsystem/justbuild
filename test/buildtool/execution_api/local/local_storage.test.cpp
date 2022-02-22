#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/local/local_storage.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from bytes",
                 "[execution_api]") {
    std::string test_bytes("test");

    LocalStorage storage{};
    auto test_digest = ArtifactDigest::Create(test_bytes);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_digest, true));
    CHECK(not storage.BlobPath(test_digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_digest, true));
    CHECK(not storage.BlobPath(test_digest, false));

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(test_bytes, false));

        auto file_path = storage.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(test_bytes, true));

        auto file_path = storage.BlobPath(test_digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from non-executable file",
                 "[execution_api]") {
    std::filesystem::path non_exec_file{
        "test/buildtool/execution_api/data/non_executable_file"};

    LocalStorage storage{};
    auto test_blob = CreateBlobFromFile(non_exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    SECTION("Add blob to storage without specifying x-bit") {
        CHECK(storage.StoreBlob(non_exec_file));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(non_exec_file, false));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(non_exec_file, true));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "LocalStorage: Add blob to storage from executable file",
                 "[execution_api]") {
    std::filesystem::path exec_file{
        "test/buildtool/execution_api/data/executable_file"};

    LocalStorage storage{};
    auto test_blob = CreateBlobFromFile(exec_file);
    REQUIRE(test_blob);

    // check blob not in storage
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    // ensure previous calls did not accidentially create the blob
    CHECK(not storage.BlobPath(test_blob->digest, true));
    CHECK(not storage.BlobPath(test_blob->digest, false));

    SECTION("Add blob to storage without specifying x-bit") {
        CHECK(storage.StoreBlob(exec_file));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }

    SECTION("Add non-executable blob to storage") {
        CHECK(storage.StoreBlob(exec_file, false));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }

    SECTION("Add executable blob to storage") {
        CHECK(storage.StoreBlob(exec_file, true));

        auto file_path = storage.BlobPath(test_blob->digest, false);
        REQUIRE(file_path);
        CHECK(FileSystemManager::IsFile(*file_path));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));

        auto exe_path = storage.BlobPath(test_blob->digest, true);
        REQUIRE(exe_path);
        CHECK(FileSystemManager::IsFile(*exe_path));
        CHECK(FileSystemManager::IsExecutable(*exe_path, true));
        CHECK(not FileSystemManager::IsExecutable(*file_path, true));
    }
}
