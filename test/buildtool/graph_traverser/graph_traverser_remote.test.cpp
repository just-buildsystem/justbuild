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

#include <optional>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/auth/authentication.hpp"
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"

TEST_CASE("Remote: Output created and contents are correct",
          "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestHelloWorldCopyMessage(auth ? &*auth : nullptr,
                              false /* not hermetic */);
}

TEST_CASE("Remote: Output created when entry point is local artifact",
          "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestCopyLocalFile(auth ? &*auth : nullptr, false /* not hermetic */);
}

TEST_CASE("Remote: Actions are not re-run", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestSequencePrinterBuildLibraryOnly(auth ? &*auth : nullptr,
                                        false /* not hermetic */);
}

TEST_CASE("Remote: KNOWN artifact", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestHelloWorldWithKnownSource(auth ? &*auth : nullptr,
                                  false /* not hermetic */);
}

TEST_CASE("Remote: Blobs uploaded and correctly used", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestBlobsUploadedAndUsed(auth ? &*auth : nullptr, false /* not hermetic */);
}

TEST_CASE("Remote: Environment variables are set and used",
          "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestEnvironmentVariablesSetAndUsed(auth ? &*auth : nullptr,
                                       false /* not hermetic */);
}

TEST_CASE("Remote: Trees correctly used", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestTreesUsed(auth ? &*auth : nullptr, false /* not hermetic */);
}

TEST_CASE("Remote: Nested trees correctly used", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestNestedTreesUsed(auth ? &*auth : nullptr, false /* not hermetic */);
}

TEST_CASE("Remote: Detect flaky actions", "[graph_traverser]") {
    std::optional<Auth::TLS> auth = {};
    if (Auth::Instance().GetAuthMethod() == AuthMethod::kTLS) {
        auth = Auth::TLS::Instance();
    }
    TestFlakyHelloWorldDetected(auth ? &*auth : nullptr,
                                false /* not hermetic */);
}
