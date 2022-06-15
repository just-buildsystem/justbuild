#include "catch2/catch.hpp"
#include "src/buildtool/crypto/hasher.hpp"

template <Hasher::HashType type>
void test_increment_hash(std::string const& bytes, std::string const& result) {
    Hasher hasher{type};
    hasher.Update(bytes.substr(0, bytes.size() / 2));
    hasher.Update(bytes.substr(bytes.size() / 2));
    auto digest = std::move(hasher).Finalize();
    CHECK(digest.HexString() == result);
}

TEST_CASE("Hasher", "[crypto]") {
    std::string bytes{"test"};

    SECTION("SHA-1") {
        // same as: echo -n test | sha1sum
        test_increment_hash<Hasher::HashType::SHA1>(
            bytes, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3");
    }

    SECTION("SHA-256") {
        // same as: echo -n test | sha256sum
        test_increment_hash<Hasher::HashType::SHA256>(
            bytes,
            "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
    }
}
