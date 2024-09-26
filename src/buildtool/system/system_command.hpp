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

#ifndef INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_COMMAND_HPP
#define INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_COMMAND_HPP

#include <array>
#include <cerrno>  // for errno
#include <cstdio>
#include <cstring>  // for strerror()
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>  // std::move
#include <vector>

#ifdef __unix__
#include <sys/wait.h>
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include "gsl/gsl"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/system.hpp"

/// \brief Execute system commands and obtain stdout, stderr and return value.
/// Subsequent commands are context free and are not affected by previous
/// commands. This class is not thread-safe.
class SystemCommand {
  public:
    /// \brief Create execution system with name.
    explicit SystemCommand(std::string name) : logger_{std::move(name)} {}

    /// \brief Execute command and arguments.
    /// Stdout and stderr can be read from files named 'stdout' and 'stderr'
    /// created in `outdir`. Those files must not exist before the execution.
    /// \param argv     argv vector with the command to execute
    /// \param env      Environment variables set for execution.
    /// \param cwd      Working directory for execution.
    /// \param outdir   Directory for storing stdout/stderr files.
    /// \returns The command's exit code, or std::nullopt on execution error.
    [[nodiscard]] auto Execute(std::vector<std::string> argv,
                               std::map<std::string, std::string> env,
                               std::filesystem::path const& cwd,
                               std::filesystem::path const& outdir) noexcept
        -> std::optional<int> {
        if (not FileSystemManager::IsDirectory(outdir)) {
            logger_.Emit(LogLevel::Error,
                         "Output directory does not exist {}",
                         outdir.string());
            return std::nullopt;
        }

        if (argv.empty()) {
            logger_.Emit(LogLevel::Error, "Command cannot be empty.");
            return std::nullopt;
        }

        std::vector<char*> cmd = UnwrapStrings(&argv);

        std::vector<std::string> env_string{};
        std::transform(std::begin(env),
                       std::end(env),
                       std::back_inserter(env_string),
                       [](auto& name_value) {
                           return name_value.first + "=" + name_value.second;
                       });
        std::vector<char*> envp = UnwrapStrings(&env_string);
        return ExecuteCommand(cmd.data(), envp.data(), cwd, outdir);
    }

  private:
    Logger logger_;

    /// \brief Open file exclusively as write-only.
    [[nodiscard]] static auto OpenFile(
        std::filesystem::path const& file_path) noexcept {
        static auto file_closer = [](gsl::owner<FILE*> f) {
            if (f != nullptr) {
                std::fclose(f);
            }
        };
        return std::unique_ptr<FILE, decltype(file_closer)>(
            std::fopen(file_path.c_str(), "wx"), file_closer);
    }

    /// \brief Execute command and arguments.
    /// \param cmd      Command arguments as char pointer array.
    /// \param envp     Environment variables as char pointer array.
    /// \param cwd      Working directory for execution.
    /// \param outdir   Directory for storing stdout/stderr files.
    /// \returns ExecOutput if command was successfully submitted to the system.
    /// \returns std::nullopt on internal failure.
    [[nodiscard]] auto ExecuteCommand(
        char* const* cmd,
        char* const* envp,
        std::filesystem::path const& cwd,
        std::filesystem::path const& outdir) noexcept -> std::optional<int> {
        auto stdout_file = outdir / "stdout";
        auto stderr_file = outdir / "stderr";
        if (auto const out = OpenFile(stdout_file)) {
            if (auto const err = OpenFile(stderr_file)) {
                if (auto retval = ForkAndExecute(
                        cmd, envp, cwd, fileno(out.get()), fileno(err.get()))) {
                    return retval;
                }
            }
            else {
                logger_.Emit(LogLevel::Error,
                             "Failed to open stderr file '{}' with error: {}",
                             stderr_file.string(),
                             strerror(errno));
            }
        }
        else {
            logger_.Emit(LogLevel::Error,
                         "Failed to open stdout file '{}' with error: {}",
                         stdout_file.string(),
                         strerror(errno));
        }

        return std::nullopt;
    }

    /// \brief Fork process and exec command.
    /// \param cmd      Command arguments as char pointer array.
    /// \param envp     Environment variables as char pointer array.
    /// \param cwd      Working directory for execution.
    /// \param out_fd   File descriptor to standard output file.
    /// \param err_fd   File descriptor to standard erro file.
    /// \returns return code if command was successfully submitted to system.
    /// \returns std::nullopt if fork or exec failed.
    [[nodiscard]] auto ForkAndExecute(char* const* cmd,
                                      char* const* envp,
                                      std::filesystem::path const& cwd,
                                      int out_fd,
                                      int err_fd) const noexcept
        -> std::optional<int> {
        auto const* cwd_cstr = cwd.c_str();

        // some executables require an open (possibly seekable) stdin, and
        // therefore, we use an open temporary file that does not appear on the
        // file system and will be removed automatically once the descriptor is
        // closed.
        gsl::owner<FILE*> in_file = std::tmpfile();
        auto in_fd = fileno(in_file);

        // fork child process
        pid_t pid = ::fork();
        if (-1 == pid) {
            logger_.Emit(LogLevel::Error,
                         "Failed to execute '{}': cannot fork a child process.",
                         *cmd);
            return std::nullopt;
        }

        // dispatch child/parent process
        if (pid == 0) {
            ::chdir(cwd_cstr);

            // redirect and close fds
            ::dup2(in_fd, STDIN_FILENO);
            ::dup2(out_fd, STDOUT_FILENO);
            ::dup2(err_fd, STDERR_FILENO);
            ::close(in_fd);
            ::close(out_fd);
            ::close(err_fd);

            // execute command in child process and exit
            ::execvpe(*cmd, cmd, envp);

            // report error and terminate child process if ::execvp did not exit
            // NOLINTNEXTLINE
            printf("Failed to execute '%s' with error: %s\n",
                   *cmd,
                   strerror(errno));

            System::ExitWithoutCleanup(EXIT_FAILURE);
        }

        ::close(in_fd);

        // wait for child to finish and obtain return value
        int status{};
        std::optional<int> retval{std::nullopt};
        do {
            if (::waitpid(pid, &status, 0) == -1) {
                // this should never happen
                logger_.Emit(LogLevel::Error,
                             "Waiting for child failed with: {}",
                             strerror(errno));
                break;
            }

            if (WIFEXITED(status)) {           // NOLINT(hicpp-signed-bitwise)
                retval = WEXITSTATUS(status);  // NOLINT(hicpp-signed-bitwise)
            }
            else if (WIFSIGNALED(status)) {  // NOLINT(hicpp-signed-bitwise)
                constexpr auto kSignalBit = 128;
                auto sig = WTERMSIG(status);  // NOLINT(hicpp-signed-bitwise)
                retval = kSignalBit + sig;
                logger_.Emit(
                    LogLevel::Debug, "Child got killed by signal {}", sig);
            }
            // continue waitpid() in case we got STOPSIG from child
        } while (not retval);

        return retval;
    }

    static auto UnwrapStrings(std::vector<std::string>* v) noexcept
        -> std::vector<char*> {
        std::vector<char*> raw{};
        std::transform(std::begin(*v),
                       std::end(*v),
                       std::back_inserter(raw),
                       [](auto& str) { return str.data(); });
        raw.push_back(nullptr);
        return raw;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_COMMAND_HPP
