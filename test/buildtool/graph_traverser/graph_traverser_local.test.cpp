#include "catch2/catch.hpp"
#include "test/buildtool/graph_traverser/graph_traverser.test.hpp"
#include "test/utils/hermeticity/local.hpp"

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Output created when entry point is local artifact",
                 "[graph_traverser]") {
    TestCopyLocalFile(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Output created and contents are correct",
                 "[graph_traverser]") {
    TestHelloWorldCopyMessage(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Actions are not re-run",
                 "[graph_traverser]") {
    TestSequencePrinterBuildLibraryOnly(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: KNOWN artifact",
                 "[graph_traverser]") {
    TestHelloWorldWithKnownSource(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Blobs uploaded and correctly used",
                 "[graph_traverser]") {
    TestBlobsUploadedAndUsed(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Environment variables are set and used",
                 "[graph_traverser]") {
    TestEnvironmentVariablesSetAndUsed(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Trees correctly used",
                 "[graph_traverser]") {
    TestTreesUsed(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Nested trees correctly used",
                 "[graph_traverser]") {
    TestNestedTreesUsed(true);
}

TEST_CASE_METHOD(HermeticLocalTestFixture,
                 "Local: Detect flaky actions",
                 "[graph_traverser]") {
    TestFlakyHelloWorldDetected(true);
}
