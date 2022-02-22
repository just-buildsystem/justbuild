#include <algorithm>

#include "catch2/catch.hpp"
#include "src/buildtool/crypto/hash_generator.hpp"

template <HashGenerator::HashType type>
void test_single_hash(std::string const& bytes, std::string const& result) {
    HashGenerator hash_gen{type};
    auto digest = hash_gen.Run(bytes);
    CHECK(digest.HexString() == result);
}

template <HashGenerator::HashType type>
void test_increment_hash(std::string const& bytes, std::string const& result) {
    HashGenerator hash_gen{type};
    auto hasher = hash_gen.IncrementalHasher();
    hasher.Update(bytes);
    auto digest = std::move(hasher).Finalize();
    CHECK(digest);
    CHECK(digest->HexString() == result);
}

TEST_CASE("Hash Generator", "[crypto]") {
    std::string bytes{"test"};

    SECTION("MD5") {
        // same as: echo -n test | md5sum
        test_single_hash<HashGenerator::HashType::MD5>(
            bytes, "098f6bcd4621d373cade4e832627b4f6");
        test_increment_hash<HashGenerator::HashType::MD5>(
            bytes, "098f6bcd4621d373cade4e832627b4f6");
    }

    SECTION("SHA-1") {
        // same as: echo -n test | sha1sum
        test_single_hash<HashGenerator::HashType::SHA1>(
            bytes, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
        test_increment_hash<HashGenerator::HashType::SHA1>(
            bytes, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
    }

    SECTION("SHA-256") {
        // same as: echo -n test | sha256sum
        test_single_hash<HashGenerator::HashType::SHA256>(
            bytes,
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
        test_increment_hash<HashGenerator::HashType::SHA256>(
            bytes,
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
    }

    SECTION("Git") {
        // same as: echo -n test | git hash-object --stdin
        test_single_hash<HashGenerator::HashType::GIT>(
            bytes, "30d74d258442c7c65512eafab474568dd706c430");
    }
}
