#include "src/buildtool/system/system.hpp"

#include <array>
#include <cstdlib>
#include <string>

#include <unistd.h>

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
#endif
}
