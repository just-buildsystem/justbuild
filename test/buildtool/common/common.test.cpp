#include "catch2/catch.hpp"
#include "src/buildtool/common/artifact.hpp"

TEST_CASE("ObjectInfo::LiberalFromString", "[artifcat]") {
    auto expected = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]");
    auto expected_as_tree = *Artifact::ObjectInfo::FromString(
        "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:0:t]");

    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f]") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "[5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:f") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:file") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:11:notavalidletter") ==
          expected);

    // Without size, which is not honored in equality
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689") == expected);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:") == expected);
    // Syntactically invalid size should be ignored
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:xyz") == expected);

    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689::t") ==
          expected_as_tree);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689::tree") ==
          expected_as_tree);
    CHECK(Artifact::ObjectInfo::LiberalFromString(
              "5e1c309dae7f45e0f39b1bf3ac3cd9db12e7d689:xyz:t") ==
          expected_as_tree);
}
