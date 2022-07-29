#include <string>

#include "catch2/catch.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_network.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"

constexpr std::size_t kLargeSize = GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH + 1;

TEST_CASE("Bazel network: write/read blobs", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();
    std::string instance_name{"remote-execution"};
    auto network = BazelNetwork{instance_name, info->host, info->port, {}};

    std::string content_foo("foo");
    std::string content_bar("bar");
    std::string content_baz(kLargeSize, 'x');  // single larger blob

    BazelBlob foo{ArtifactDigest::Create(content_foo), content_foo};
    BazelBlob bar{ArtifactDigest::Create(content_bar), content_bar};
    BazelBlob baz{ArtifactDigest::Create(content_baz), content_baz};

    // Search blobs via digest
    REQUIRE(network.UploadBlobs(BlobContainer{{foo, bar, baz}}));

    // Read blobs in order
    auto reader = network.ReadBlobs(
        {foo.digest, bar.digest, baz.digest, bar.digest, foo.digest});
    std::vector<BazelBlob> blobs{};
    while (true) {
        auto next = reader.Next();
        if (next.empty()) {
            break;
        }
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 5);
    CHECK(blobs[0].data == content_foo);
    CHECK(blobs[1].data == content_bar);
    CHECK(blobs[2].data == content_baz);
    CHECK(blobs[3].data == content_bar);
    CHECK(blobs[4].data == content_foo);
}

TEST_CASE("Bazel network: read blobs with unknown size", "[execution_api]") {
    if (Compatibility::IsCompatible()) {
        // only supported in native mode
        return;
    }

    auto const& info = RemoteExecutionConfig::RemoteAddress();
    std::string instance_name{"remote-execution"};
    auto network = BazelNetwork{instance_name, info->host, info->port, {}};

    std::string content_foo("foo");
    std::string content_bar(kLargeSize, 'x');  // single larger blob

    BazelBlob foo{ArtifactDigest::Create(content_foo), content_foo};
    BazelBlob bar{ArtifactDigest::Create(content_bar), content_bar};

    // Upload blobs
    REQUIRE(network.UploadBlobs(BlobContainer{{foo, bar}}));

    // Set size to unknown
    foo.digest.set_size_bytes(0);
    bar.digest.set_size_bytes(0);

    // Read blobs
    auto reader = network.ReadBlobs({foo.digest, bar.digest});
    std::vector<BazelBlob> blobs{};
    while (true) {
        auto next = reader.Next();
        if (next.empty()) {
            break;
        }
        blobs.insert(blobs.end(), next.begin(), next.end());
    }

    // Check order maintained
    REQUIRE(blobs.size() == 2);
    CHECK(blobs[0].data == content_foo);
    CHECK(blobs[1].data == content_bar);
}
