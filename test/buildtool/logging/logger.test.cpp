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

#include "src/buildtool/logging/logger.hpp"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"

// Stores prints from test sink instances
class TestPrints {
    struct PrintData {
        std::atomic<int> counter{};
        std::unordered_map<int, std::vector<std::string>> prints{};
    };

  public:
    static void Print(int sink_id, std::string const& print) noexcept {
        Data().prints[sink_id].push_back(print);
    }
    [[nodiscard]] static auto Read(int sink_id) noexcept
        -> std::vector<std::string> {
        return Data().prints[sink_id];
    }

    static void Clear() noexcept {
        Data().prints.clear();
        Data().counter = 0;
    }

    static auto GetId() noexcept -> int { return Data().counter++; }

  private:
    [[nodiscard]] static auto Data() noexcept -> PrintData& {
        static PrintData instance{};
        return instance;
    }
};

// Test sink, prints to TestPrints depending on its own instance id.
class LogSinkTest : public ILogSink {
  public:
    static auto CreateFactory() -> LogSinkFactory {
        return [] { return std::make_shared<LogSinkTest>(); };
    }

    LogSinkTest() noexcept { id_ = TestPrints::GetId(); }

    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg) const noexcept final {
        auto prefix = LogLevelToString(level);

        if (logger != nullptr) {
            prefix += " (" + logger->Name() + ")";
        }

        TestPrints::Print(id_, prefix + ": " + msg);
    }

  private:
    int id_{};
};

class OneGlobalSinkFixture {
  public:
    OneGlobalSinkFixture() {
        TestPrints::Clear();
        LogConfig::SetLogLimit(LogLevel::Info);
        LogConfig::SetSinks({LogSinkTest::CreateFactory()});
    }
};

class TwoGlobalSinksFixture : public OneGlobalSinkFixture {
  public:
    TwoGlobalSinksFixture() {
        LogConfig::AddSink(LogSinkTest::CreateFactory());
    }
};

TEST_CASE_METHOD(OneGlobalSinkFixture,
                 "Global static logger with one sink",
                 "[logger]") {
    // logs should be forwarded to sink instance: 0
    int instance = 0;

    // create log outside of log limit
    Logger::Log(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance).empty());

    SECTION("create log within log limit") {
        Logger::Log(LogLevel::Info, "second");
        auto prints = TestPrints::Read(instance);
        REQUIRE(prints.size() == 1);
        CHECK(prints[0] == "INFO: second");

        SECTION("increase log limit create log within log limit") {
            LogConfig::SetLogLimit(LogLevel::Trace);
            Logger::Log(LogLevel::Trace, "third");
            auto prints = TestPrints::Read(instance);
            REQUIRE(prints.size() == 2);
            CHECK(prints[1] == "TRACE: third");

            SECTION("log via lambda function") {
                Logger::Log(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints = TestPrints::Read(instance);
                REQUIRE(prints.size() == 3);
                CHECK(prints[2] == "TRACE: forth");
            }
        }
    }
}

TEST_CASE_METHOD(OneGlobalSinkFixture,
                 "Local named logger using one global sink",
                 "[logger]") {
    // create logger with sink instances from global LogConfig
    Logger logger("TestLogger");

    // logs should be forwarded to same sink instance as before: 0
    int instance = 0;

    // create log outside of log limit
    logger.Emit(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance).empty());

    SECTION("create log within log limit") {
        logger.Emit(LogLevel::Info, "second");
        auto prints = TestPrints::Read(instance);
        REQUIRE(prints.size() == 1);
        CHECK(prints[0] == "INFO (TestLogger): second");

        SECTION("increase log limit create log within log limit") {
            logger.SetLogLimit(LogLevel::Trace);
            logger.Emit(LogLevel::Trace, "third");
            auto prints = TestPrints::Read(instance);
            REQUIRE(prints.size() == 2);
            CHECK(prints[1] == "TRACE (TestLogger): third");

            SECTION("log via lambda function") {
                logger.Emit(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints = TestPrints::Read(instance);
                REQUIRE(prints.size() == 3);
                CHECK(prints[2] == "TRACE (TestLogger): forth");
            }
        }
    }
}

TEST_CASE_METHOD(OneGlobalSinkFixture,
                 "Local named logger with its own sink instance"
                 "[logger]") {
    // create logger with separate sink instance
    Logger logger("OwnSinkLogger", {LogSinkTest::CreateFactory()});

    // logs should be forwarded to new sink instance: 1
    int instance = 1;

    // create log outside of log limit
    logger.Emit(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance).empty());

    SECTION("create log within log limit") {
        logger.Emit(LogLevel::Info, "second");
        auto prints = TestPrints::Read(instance);
        REQUIRE(prints.size() == 1);
        CHECK(prints[0] == "INFO (OwnSinkLogger): second");

        SECTION("increase log limit create log within log limit") {
            logger.SetLogLimit(LogLevel::Trace);
            logger.Emit(LogLevel::Trace, "third");
            auto prints = TestPrints::Read(instance);
            REQUIRE(prints.size() == 2);
            CHECK(prints[1] == "TRACE (OwnSinkLogger): third");

            SECTION("log via lambda function") {
                logger.Emit(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints = TestPrints::Read(instance);
                REQUIRE(prints.size() == 3);
                CHECK(prints[2] == "TRACE (OwnSinkLogger): forth");
            }
        }
    }
}

TEST_CASE_METHOD(TwoGlobalSinksFixture,
                 "Global static logger with two sinks",
                 "[logger]") {
    // logs should be forwarded to sink instances: 0 and 1
    int instance1 = 0;
    int instance2 = 1;

    // create log outside of log limit
    Logger::Log(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance1).empty());
    CHECK(TestPrints::Read(instance2).empty());

    SECTION("create log within log limit") {
        Logger::Log(LogLevel::Info, "second");
        auto prints1 = TestPrints::Read(instance1);
        auto prints2 = TestPrints::Read(instance2);
        REQUIRE(prints1.size() == 1);
        REQUIRE(prints2.size() == 1);
        CHECK(prints1[0] == "INFO: second");
        CHECK(prints2[0] == "INFO: second");

        SECTION("increase log limit create log within log limit") {
            LogConfig::SetLogLimit(LogLevel::Trace);
            Logger::Log(LogLevel::Trace, "third");
            auto prints1 = TestPrints::Read(instance1);
            auto prints2 = TestPrints::Read(instance2);
            REQUIRE(prints1.size() == 2);
            REQUIRE(prints2.size() == 2);
            CHECK(prints1[1] == "TRACE: third");
            CHECK(prints2[1] == "TRACE: third");

            SECTION("log via lambda function") {
                Logger::Log(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints1 = TestPrints::Read(instance1);
                auto prints2 = TestPrints::Read(instance2);
                REQUIRE(prints1.size() == 3);
                REQUIRE(prints2.size() == 3);
                CHECK(prints1[2] == "TRACE: forth");
                CHECK(prints2[2] == "TRACE: forth");
            }
        }
    }
}

TEST_CASE_METHOD(TwoGlobalSinksFixture,
                 "Local named logger using two global sinks",
                 "[logger]") {
    // create logger with sink instances from global LogConfig
    Logger logger("TestLogger");

    // logs should be forwarded to same sink instances: 0 and 1
    int instance1 = 0;
    int instance2 = 1;

    // create log outside of log limit
    logger.Emit(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance1).empty());
    CHECK(TestPrints::Read(instance2).empty());

    SECTION("create log within log limit") {
        logger.Emit(LogLevel::Info, "second");
        auto prints1 = TestPrints::Read(instance1);
        auto prints2 = TestPrints::Read(instance2);
        REQUIRE(prints1.size() == 1);
        REQUIRE(prints2.size() == 1);
        CHECK(prints1[0] == "INFO (TestLogger): second");
        CHECK(prints2[0] == "INFO (TestLogger): second");

        SECTION("increase log limit create log within log limit") {
            logger.SetLogLimit(LogLevel::Trace);
            logger.Emit(LogLevel::Trace, "third");
            auto prints1 = TestPrints::Read(instance1);
            auto prints2 = TestPrints::Read(instance2);
            REQUIRE(prints1.size() == 2);
            REQUIRE(prints2.size() == 2);
            CHECK(prints1[1] == "TRACE (TestLogger): third");
            CHECK(prints2[1] == "TRACE (TestLogger): third");

            SECTION("log via lambda function") {
                logger.Emit(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints1 = TestPrints::Read(instance1);
                auto prints2 = TestPrints::Read(instance2);
                REQUIRE(prints1.size() == 3);
                REQUIRE(prints2.size() == 3);
                CHECK(prints1[2] == "TRACE (TestLogger): forth");
                CHECK(prints2[2] == "TRACE (TestLogger): forth");
            }
        }
    }
}

TEST_CASE_METHOD(TwoGlobalSinksFixture,
                 "Local named logger with its own two sink instances",
                 "[logger]") {
    // create logger with separate sink instances
    Logger logger("OwnSinkLogger",
                  {LogSinkTest::CreateFactory(), LogSinkTest::CreateFactory()});

    // logs should be forwarded to new sink instances: 2 and 3
    int instance1 = 2;
    int instance2 = 3;

    // create log outside of log limit
    logger.Emit(LogLevel::Trace, "first");
    CHECK(TestPrints::Read(instance1).empty());
    CHECK(TestPrints::Read(instance2).empty());

    SECTION("create log within log limit") {
        logger.Emit(LogLevel::Info, "second");
        auto prints1 = TestPrints::Read(instance1);
        auto prints2 = TestPrints::Read(instance2);
        REQUIRE(prints1.size() == 1);
        REQUIRE(prints2.size() == 1);
        CHECK(prints1[0] == "INFO (OwnSinkLogger): second");
        CHECK(prints2[0] == "INFO (OwnSinkLogger): second");

        SECTION("increase log limit create log within log limit") {
            logger.SetLogLimit(LogLevel::Trace);
            logger.Emit(LogLevel::Trace, "third");
            auto prints1 = TestPrints::Read(instance1);
            auto prints2 = TestPrints::Read(instance2);
            REQUIRE(prints1.size() == 2);
            REQUIRE(prints2.size() == 2);
            CHECK(prints1[1] == "TRACE (OwnSinkLogger): third");
            CHECK(prints2[1] == "TRACE (OwnSinkLogger): third");

            SECTION("log via lambda function") {
                logger.Emit(LogLevel::Trace,
                            [] { return std::string{"forth"}; });
                auto prints1 = TestPrints::Read(instance1);
                auto prints2 = TestPrints::Read(instance2);
                REQUIRE(prints1.size() == 3);
                REQUIRE(prints2.size() == 3);
                CHECK(prints1[2] == "TRACE (OwnSinkLogger): forth");
                CHECK(prints2[2] == "TRACE (OwnSinkLogger): forth");
            }
        }
    }
}

TEST_CASE_METHOD(OneGlobalSinkFixture,
                 "Common interface for global and local named loggers"
                 "[logger]") {
    // global logs will be forwarded to instance: 0
    int global_instance = 0;

    // create local logger with separate sink instance
    Logger logger("OwnSinkLogger", {LogSinkTest::CreateFactory()});
    // local logs should be forwarded to new sink instance: 1
    int local_instance = 1;

    SECTION("global instance") {
        // create log outside of log limit
        Logger::Log(nullptr, LogLevel::Trace, "first");
        CHECK(TestPrints::Read(global_instance).empty());

        SECTION("create log within log limit") {
            Logger::Log(nullptr, LogLevel::Info, "second");
            auto prints = TestPrints::Read(global_instance);
            REQUIRE(prints.size() == 1);
            CHECK(prints[0] == "INFO: second");

            SECTION("increase log limit create log within log limit") {
                LogConfig::SetLogLimit(LogLevel::Trace);
                Logger::Log(nullptr, LogLevel::Trace, "third");
                auto prints = TestPrints::Read(global_instance);
                REQUIRE(prints.size() == 2);
                CHECK(prints[1] == "TRACE: third");

                SECTION("log via lambda function") {
                    Logger::Log(nullptr, LogLevel::Trace, [] {
                        return std::string{"forth"};
                    });
                    auto prints = TestPrints::Read(global_instance);
                    REQUIRE(prints.size() == 3);
                    CHECK(prints[2] == "TRACE: forth");
                }
            }
        }
    }

    SECTION("named instance") {
        // create log outside of log limit
        Logger::Log(&logger, LogLevel::Trace, "first");
        CHECK(TestPrints::Read(local_instance).empty());

        SECTION("create log within log limit") {
            Logger::Log(&logger, LogLevel::Info, "second");
            auto prints = TestPrints::Read(local_instance);
            REQUIRE(prints.size() == 1);
            CHECK(prints[0] == "INFO (OwnSinkLogger): second");

            SECTION("increase log limit create log within log limit") {
                logger.SetLogLimit(LogLevel::Trace);
                Logger::Log(&logger, LogLevel::Trace, "third");
                auto prints = TestPrints::Read(local_instance);
                REQUIRE(prints.size() == 2);
                CHECK(prints[1] == "TRACE (OwnSinkLogger): third");

                SECTION("log via lambda function") {
                    Logger::Log(&logger, LogLevel::Trace, [] {
                        return std::string{"forth"};
                    });
                    auto prints = TestPrints::Read(local_instance);
                    REQUIRE(prints.size() == 3);
                    CHECK(prints[2] == "TRACE (OwnSinkLogger): forth");
                }
            }
        }
    }
}
