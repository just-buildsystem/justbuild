#include "catch2/catch.hpp"
#include "src/buildtool/main/install_cas.hpp"

TEST_CASE("ObjectInfoFromLiberalString", "[artifact]") {
    auto expected = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]");
    auto expected_as_tree = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:0:t]");

    CHECK(ObjectInfoFromLiberalString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:file") == expected);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      ":11:notavalidletter") == expected);

    // Without size, which is not honored in equality
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689") == expected);
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:") == expected);
    // Syntactically invalid size should be ignored
    CHECK(ObjectInfoFromLiberalString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:xyz") == expected);

    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      "::t") == expected_as_tree);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      "::tree") == expected_as_tree);
    CHECK(ObjectInfoFromLiberalString("5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689"
                                      ":xyz:t") == expected_as_tree);
}
