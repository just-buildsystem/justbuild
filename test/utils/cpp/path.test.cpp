#include <filesystem>

#include "catch2/catch.hpp"
#include "src/utils/cpp/path.hpp"

TEST_CASE("normalization", "[path]") {
    CHECK(ToNormalPath(std::filesystem::path{""}) ==
          ToNormalPath(std::filesystem::path{"."}));
    CHECK(ToNormalPath(std::filesystem::path{""}).string() == ".");
    CHECK(ToNormalPath(std::filesystem::path{"."}).string() == ".");

    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/.."}).string() == "foo");
    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/../"}).string() == "foo");
    CHECK(ToNormalPath(std::filesystem::path{"foo/bar/../baz"}).string() ==
          "foo/baz");
    CHECK(ToNormalPath(std::filesystem::path{"./foo/bar"}).string() ==
          "foo/bar");
    CHECK(ToNormalPath(std::filesystem::path{"foo/.."}).string() == ".");
    CHECK(ToNormalPath(std::filesystem::path{"./foo/.."}).string() == ".");
}
