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

#include "src/buildtool/multithreading/task.hpp"

#include <functional>
#include <utility>  // std::move

#include "catch2/catch_test_macros.hpp"

namespace {
constexpr int kDummyValue{5};
struct StatelessCallable {
    void operator()() noexcept {}
};

struct ValueCaptureCallable {
    explicit ValueCaptureCallable(int i) noexcept : number{i} {}

    // NOLINTNEXTLINE
    void operator()() noexcept { number += kDummyValue; }

    int number;
};

struct RefCaptureCallable {
    // NOLINTNEXTLINE(google-runtime-references)
    explicit RefCaptureCallable(int& i) noexcept : number{i} {}

    // NOLINTNEXTLINE
    void operator()() noexcept { number += 3; }

    int& number;
};

}  // namespace

TEST_CASE("Default constructed task is empty", "[task]") {
    Task t;
    CHECK(not t);
    CHECK(not(Task()));
    CHECK(not(Task{}));
}

TEST_CASE("Task constructed from empty function is empty", "[task]") {
    std::function<void()> empty_function;
    Task t_from_empty_function{empty_function};

    CHECK(not Task(std::function<void()>{}));
    CHECK(not Task(empty_function));
    CHECK(not t_from_empty_function);
}

TEST_CASE("Task constructed from user defined callable object is not empty",
          "[task]") {
    SECTION("Stateless struct") {
        Task t{StatelessCallable{}};
        StatelessCallable callable;
        Task t_from_named_callable{callable};

        CHECK(Task{StatelessCallable{}});
        CHECK(Task{callable});
        CHECK(t);
        CHECK(t_from_named_callable);
    }

    SECTION("Statefull struct") {
        SECTION("Reference capture") {
            int a = 2;
            Task t_ref{RefCaptureCallable{a}};
            RefCaptureCallable three_adder{a};
            Task t_from_named_callable_ref_capture{three_adder};

            CHECK(Task{RefCaptureCallable{a}});
            CHECK(Task{three_adder});
            CHECK(t_ref);
            CHECK(t_from_named_callable_ref_capture);
        }

        SECTION("Value capture") {
            Task t_value{ValueCaptureCallable{1}};
            ValueCaptureCallable callable{2};
            Task t_from_named_callable_value_capture{callable};

            CHECK(Task{ValueCaptureCallable{3}});
            CHECK(Task{callable});
            CHECK(t_value);
            CHECK(t_from_named_callable_value_capture);
        }
    }
}

TEST_CASE("Task constructed from lambda is not empty", "[task]") {
    SECTION("Stateless lambda") {
        Task t{[]() {}};
        auto callable = []() {};
        Task t_from_named_callable{callable};

        CHECK(Task{[]() {}});
        CHECK(Task{callable});
        CHECK(t);
        CHECK(t_from_named_callable);
    }

    SECTION("Statefull lambda") {
        SECTION("Reference capture") {
            int a = 2;
            Task t_ref{[&a]() { a += 3; }};
            auto lambda = [&a]() { a += 3; };
            Task t_from_named_lambda_ref_capture{lambda};

            CHECK(Task{[&a]() { a += 3; }});
            CHECK(Task{lambda});
            CHECK(t_ref);
            CHECK(t_from_named_lambda_ref_capture);
        }

        SECTION("Value capture") {
            int a = 1;
            // NOLINTNEXTLINE
            Task t_value{[num = a]() mutable {
                num += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(num);
            }};
            // NOLINTNEXTLINE
            auto lambda = [num = a]() mutable {
                num += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(num);
            };
            Task t_from_named_lambda_value_capture{lambda};

            CHECK(Task{[num = a]() mutable {
                num += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(num);
            }});
            CHECK(Task{lambda});
            CHECK(t_value);
            CHECK(t_from_named_lambda_value_capture);
        }
    }
}

TEST_CASE("Task can be executed and doesn't steal contents", "[task]") {
    SECTION("User defined object") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            ValueCaptureCallable add_five{num};
            Task t_add_five{add_five};
            CHECK(add_five.number == initial_value);
            t_add_five();

            // Internal data has been copied once again to the Task, so what is
            // modified in the call to the task op() is not the data we can
            // observe from the struct we created (add_five.number)
            CHECK(add_five.number == initial_value);
            CHECK(num == initial_value);
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            RefCaptureCallable add_three{num};
            Task t_add_three{add_three};
            CHECK(add_three.number == initial_value);
            t_add_three();

            // In this case, data modified by the task is the same than the one
            // in the struct, so we can observe the change
            CHECK(add_three.number == initial_value + 3);
            CHECK(&num == &add_three.number);
        }
    }

    SECTION("Anonymous lambda function") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            Task t_add_five{[a = num]() mutable {
                a += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(a);
            }};
            t_add_five();

            // Internal data can not be observed, external data does not change
            CHECK(num == initial_value);
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            Task t_add_three{[&num]() { num += 3; }};
            t_add_three();

            // Internal data can not be observed, external data changes
            CHECK(num == initial_value + 3);
        }
    }

    SECTION("Named lambda function") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            auto add_five = [a = num]() mutable {
                a += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(a);
            };
            Task t_add_five{add_five};
            t_add_five();

            // Internal data can not be observed, external data does not change
            CHECK(num == initial_value);
            // Lambda can be still called (we can't observe side effects)
            add_five();
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            auto add_three = [&num]() { num += 3; };
            Task t_add_three{add_three};
            t_add_three();

            // Internal data can not be observed, external data changes
            CHECK(num == initial_value + 3);
            // Lambda can be still called (and side effects are as expected)
            add_three();
            CHECK(num == initial_value + 6);
        }
    }

    SECTION("std::function") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            std::function<void()> add_five{[a = num]() mutable {
                a += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(a);
            }};
            Task t_add_five{add_five};
            t_add_five();

            // Internal data can not be observed, external data does not change
            CHECK(num == initial_value);
            // Original function still valid (side effects not observable)
            CHECK(add_five);
            add_five();
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            std::function<void()> add_three{[&num]() { num += 3; }};
            Task t_add_three{add_three};
            t_add_three();

            // Internal data can not be observed, external data changes
            CHECK(num == initial_value + 3);
            // Original function still valid (and side effects are as expected)
            CHECK(add_three);
            add_three();
            CHECK(num == initial_value + 6);
        }
    }
}

TEST_CASE("Task moving from named object can be executed", "[task]") {
    // Constructing Tasks from named objects using Task{std::move(named_object)}
    // is only a way to explicitly express that the constructor from Task that
    // will be called will treat `named_object` as an rvalue (temporary object).
    // We could accomplish the same by using `Task t{Type{args}};` where `Type`
    // is the type of the callable object.
    SECTION("User defined object") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            ValueCaptureCallable add_five{num};
            // NOLINTNEXTLINE
            Task t_add_five{std::move(add_five)};
            t_add_five();

            // No observable side effects
            CHECK(num == initial_value);
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            RefCaptureCallable add_three{num};
            // NOLINTNEXTLINE
            Task t_add_three{std::move(add_three)};
            t_add_three();

            // External data must have been affected by side effect but in this
            // case `add_three` is a moved-from object so there is no guarantee
            // about the data it holds
            CHECK(num == initial_value + 3);
        }
    }

    // Note that for anonymous lambdas the move constructor of Task is the one
    // that has already been tested
    SECTION("Named lambda function") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            auto add_five = [a = num]() mutable {
                a += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(a);
            };
            Task t_add_five{std::move(add_five)};
            t_add_five();

            // Internal data can not be observed, external data does not change
            CHECK(num == initial_value);
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            auto add_three = [&num]() { num += 3; };
            Task t_add_three{std::move(add_three)};
            t_add_three();

            // Internal data can not be observed, external data changes
            CHECK(num == initial_value + 3);
        }
    }

    SECTION("std::function") {
        SECTION("Value capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            std::function<void()> add_five{[a = num]() mutable {
                a += kDummyValue;
                // get rid of "set but unused var"
                static_cast<void>(a);
            }};
            Task t_add_five{std::move(add_five)};
            t_add_five();

            // Internal data can not be observed, external data does not change
            CHECK(num == initial_value);
        }
        SECTION("Reference capture") {
            int const initial_value = 2;
            int num = initial_value;
            // NOLINTNEXTLINE
            std::function<void()> add_three{[&num]() { num += 3; }};
            Task t_add_three{std::move(add_three)};
            t_add_three();

            // Internal data can not be observed, external data changes
            CHECK(num == initial_value + 3);
        }
    }
}
