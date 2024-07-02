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

#include <cstddef>
#include <optional>
#include <string>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/remote/bazel/bytestream_client.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "test/utils/remote_execution/test_auth_config.hpp"

constexpr std::size_t kLargeSize = GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH + 1;

TEST_CASE("ByteStream Client: Transfer single blob", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);
    auto stream = ByteStreamClient{info->host, info->port, &*auth_config};
    auto uuid = CreateUUIDVersion4(*CreateProcessUniqueId());

    SECTION("Upload small blob") {
        std::string instance_name{"remote-execution"};
        std::string content("foobar");

        // digest of "foobar"
        auto digest = static_cast<bazel_re::Digest>(
            ArtifactDigest::Create<ObjectType::File>(content));

        CHECK(stream.Write(fmt::format("{}/uploads/{}/blobs/{}/{}",
                                       instance_name,
                                       uuid,
                                       digest.hash(),
                                       digest.size_bytes()),
                           content));

        SECTION("Download small blob") {
            auto data = stream.Read(fmt::format("{}/blobs/{}/{}",
                                                instance_name,
                                                digest.hash(),
                                                digest.size_bytes()));

            CHECK(data == content);
        }
    }

    SECTION("Upload large blob") {
        std::string instance_name{"remote-execution"};

        std::string content(kLargeSize, '\0');
        for (std::size_t i{}; i < content.size(); ++i) {
            content[i] = instance_name[i % instance_name.size()];
        }

        // digest of "instance_nameinstance_nameinstance_..."
        auto digest = static_cast<bazel_re::Digest>(
            ArtifactDigest::Create<ObjectType::File>(content));

        CHECK(stream.Write(fmt::format("{}/uploads/{}/blobs/{}/{}",
                                       instance_name,
                                       uuid,
                                       digest.hash(),
                                       digest.size_bytes()),
                           content));

        SECTION("Download large blob") {
            auto data = stream.Read(fmt::format("{}/blobs/{}/{}",
                                                instance_name,
                                                digest.hash(),
                                                digest.size_bytes()));

            CHECK(data == content);
        }

        SECTION("Incrementally download large blob") {
            auto reader =
                stream.IncrementalRead(fmt::format("{}/blobs/{}/{}",
                                                   instance_name,
                                                   digest.hash(),
                                                   digest.size_bytes()));

            std::string data{};
            auto chunk = reader.Next();
            while (chunk and not chunk->empty()) {
                data.append(chunk->begin(), chunk->end());
                chunk = reader.Next();
            }

            CHECK(chunk);
            CHECK(data == content);
        }
    }
}

TEST_CASE("ByteStream Client: Transfer multiple blobs", "[execution_api]") {
    auto const& info = RemoteExecutionConfig::RemoteAddress();
    auto auth_config = TestAuthConfig::ReadAuthConfigFromEnvironment();
    REQUIRE(auth_config);
    auto stream = ByteStreamClient{info->host, info->port, &*auth_config};
    auto uuid = CreateUUIDVersion4(*CreateProcessUniqueId());

    SECTION("Upload small blobs") {
        std::string instance_name{"remote-execution"};

        BazelBlob foo{ArtifactDigest::Create<ObjectType::File>("foo"),
                      "foo",
                      /*is_exec=*/false};
        BazelBlob bar{ArtifactDigest::Create<ObjectType::File>("bar"),
                      "bar",
                      /*is_exec=*/false};
        BazelBlob baz{ArtifactDigest::Create<ObjectType::File>("baz"),
                      "baz",
                      /*is_exec=*/false};

        CHECK(stream.WriteMany<BazelBlob>(
            {foo, bar, baz},
            [&instance_name, &uuid](auto const& blob) {
                return fmt::format("{}/uploads/{}/blobs/{}/{}",
                                   instance_name,
                                   uuid,
                                   blob.digest.hash(),
                                   blob.digest.size_bytes());
            },
            [](auto const& blob) { return *blob.data; }));

        SECTION("Download small blobs") {
            std::vector<std::string> contents{};
            stream.ReadMany<bazel_re::Digest>(
                {foo.digest, bar.digest, baz.digest},
                [&instance_name](auto const& digest) -> std::string {
                    return fmt::format("{}/blobs/{}/{}",
                                       instance_name,
                                       digest.hash(),
                                       digest.size_bytes());
                },
                [&contents](auto data) {
                    contents.emplace_back(std::move(data));
                });
            REQUIRE(contents.size() == 3);
            CHECK(contents[0] == *foo.data);
            CHECK(contents[1] == *bar.data);
            CHECK(contents[2] == *baz.data);
        }
    }

    SECTION("Upload large blobs") {
        std::string instance_name{"remote-execution"};

        std::string content_foo(kLargeSize, '\0');
        std::string content_bar(kLargeSize, '\0');
        std::string content_baz(kLargeSize, '\0');
        for (std::size_t i{}; i < content_foo.size(); ++i) {
            content_foo[i] = instance_name[(i + 0) % instance_name.size()];
            content_bar[i] = instance_name[(i + 1) % instance_name.size()];
            content_baz[i] = instance_name[(i + 2) % instance_name.size()];
        }

        BazelBlob foo{ArtifactDigest::Create<ObjectType::File>(content_foo),
                      content_foo,
                      /*is_exec=*/false};
        BazelBlob bar{ArtifactDigest::Create<ObjectType::File>(content_bar),
                      content_bar,
                      /*is_exec=*/false};
        BazelBlob baz{ArtifactDigest::Create<ObjectType::File>(content_baz),
                      content_baz,
                      /*is_exec=*/false};

        CHECK(stream.WriteMany<BazelBlob>(
            {foo, bar, baz},
            [&instance_name, &uuid](auto const& blob) {
                return fmt::format("{}/uploads/{}/blobs/{}/{}",
                                   instance_name,
                                   uuid,
                                   blob.digest.hash(),
                                   blob.digest.size_bytes());
            },
            [](auto const& blob) { return *blob.data; }));

        SECTION("Download large blobs") {
            std::vector<std::string> contents{};
            stream.ReadMany<bazel_re::Digest>(
                {foo.digest, bar.digest, baz.digest},
                [&instance_name](auto const& digest) -> std::string {
                    return fmt::format("{}/blobs/{}/{}",
                                       instance_name,
                                       digest.hash(),
                                       digest.size_bytes());
                },
                [&contents](auto data) {
                    contents.emplace_back(std::move(data));
                });
            REQUIRE(contents.size() == 3);
            CHECK(contents[0] == *foo.data);
            CHECK(contents[1] == *bar.data);
            CHECK(contents[2] == *baz.data);
        }
    }
}
