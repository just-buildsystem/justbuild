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

#include "src/buildtool/system/system.hpp"

#ifdef VALGRIND_BUILD
#ifdef __unix__
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif  // __unix__
#include <array>
#include <string>
#else
#include <cstdlib>
#endif  // VALGRIND_BUILD

void System::ExitWithoutCleanup(int exit_code) {
#ifdef VALGRIND_BUILD
    // Usually std::_Exit() is the right thing to do in child processes that do
    // not need to perform any cleanup (static destructors etc.). However,
    // Valgrind will trace child processes until exec(3) is called or otherwise
    // complains about leaks. Therefore, exit child processes via execvpe(3) if
    // VALGRIND_BUILD is defined.
    auto cmd =
        std::string{exit_code == EXIT_SUCCESS ? "/bin/true" : "/bin/false"};
    auto args = std::array<char*, 2>{cmd.data(), nullptr};
    ::execvpe(args[0], args.data(), nullptr);
#else
    std::_Exit(exit_code);
#endif  // VALGRIND_BUILD
}
