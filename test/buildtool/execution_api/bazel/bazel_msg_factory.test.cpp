#include <filesystem>

#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact_factory.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/file_system/object_type.hpp"

TEST_CASE("Bazel internals: MessageFactory", "[execution_api]") {
    std::filesystem::path workspace{"test/buildtool/execution_api/data"};

    std::filesystem::path subdir1 = workspace / "subdir1";
    std::filesystem::path subdir2 = subdir1 / "subdir2";
    std::filesystem::path file1 = subdir1 / "file1";
    std::filesystem::path file2 = subdir2 / "file2";

    auto file1_blob = CreateBlobFromFile(file1);
    auto file2_blob = CreateBlobFromFile(file2);

    CHECK(file1_blob);
    CHECK(file2_blob);

    // both files are the same and should result in identical blobs
    CHECK(file1_blob->data == file2_blob->data);
    CHECK(file1_blob->digest.hash() == file2_blob->digest.hash());
    CHECK(file1_blob->digest.size_bytes() == file2_blob->digest.size_bytes());

    // create known artifacts
    auto artifact1_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            file1_blob->digest.hash(),
            static_cast<std::size_t>(file1_blob->digest.size_bytes()),
            ObjectType::File));
    CHECK(artifact1_opt.has_value());
    auto artifact1 = DependencyGraph::ArtifactNode{std::move(*artifact1_opt)};

    auto artifact2_opt =
        ArtifactFactory::FromDescription(ArtifactFactory::DescribeKnownArtifact(
            file2_blob->digest.hash(),
            static_cast<std::size_t>(file2_blob->digest.size_bytes()),
            ObjectType::File));
    CHECK(artifact2_opt.has_value());
    auto artifact2 = DependencyGraph::ArtifactNode{std::move(*artifact2_opt)};

    // create blobs via tree
    BlobContainer blobs{};
    REQUIRE(BazelMsgFactory::CreateDirectoryDigestFromTree(
        {{file1.string(), &artifact1}, {file2.string(), &artifact2}},
        [&blobs](BazelBlob&& blob) { blobs.Emplace(std::move(blob)); }));

    // TODO(aehlig): also check total number of DirectoryNode blobs in container
}
