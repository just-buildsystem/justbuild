#include "catch2/catch.hpp"
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"

TEST_CASE("Remote: Output created and contents are correct",
          "[graph_traverser]") {
    TestHelloWorldCopyMessage(false /* not hermetic */);
}

TEST_CASE("Remote: Output created when entry point is local artifact",
          "[graph_traverser]") {
    TestCopyLocalFile(false /* not hermetic */);
}

TEST_CASE("Remote: Actions are not re-run", "[graph_traverser]") {
    TestSequencePrinterBuildLibraryOnly(false /* not hermetic */);
}

TEST_CASE("Remote: KNOWN artifact", "[graph_traverser]") {
    TestHelloWorldWithKnownSource(false /* not hermetic */);
}

TEST_CASE("Remote: Blobs uploaded and correctly used", "[graph_traverser]") {
    TestBlobsUploadedAndUsed(false /* not hermetic */);
}

TEST_CASE("Remote: Environment variables are set and used",
          "[graph_traverser]") {
    TestEnvironmentVariablesSetAndUsed(false /* not hermetic */);
}

TEST_CASE("Remote: Trees correctly used", "[graph_traverser]") {
    TestTreesUsed(false /* not hermetic */);
}

TEST_CASE("Remote: Nested trees correctly used", "[graph_traverser]") {
    TestNestedTreesUsed(false /* not hermetic */);
}

TEST_CASE("Remote: Detect flaky actions", "[graph_traverser]") {
    TestFlakyHelloWorldDetected(false /* not hermetic */);
}
