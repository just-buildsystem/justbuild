#ifndef INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_COMMAND_HPP
#define INCLUDED_SRC_BUILDTOOL_SYSTEM_SYSTEM_COMMAND_HPP

#include <array>
#include <cstdio>
#include <cstring>  // for strerror()
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/system.hpp"

/// \brief Execute system commands and obtain stdout, stderr and return value.
/// Subsequent commands are context free and are not affected by previous
/// commands. This class is not thread-safe.
class SystemCommand {
  public:
    struct ExecOutput {
        int return_value{};
        std::filesystem::path stdout_file{};
        std::filesystem::path stderr_file{};
    };

    /// \brief Create execution system with name.
    explicit SystemCommand(std::string name) : logger_{std::move(name)} {}

    /// \brief Execute command and arguments.
    /// \param argv     argv vector with the command to execute
    /// \param env      Environment variables set for execution.
    /// \param cwd      Working directory for execution.
    /// \param tmpdir   Temporary directory for storing stdout/stderr files.
    /// \returns std::nullopt if there was an error in the execution setup
    /// outside running the command itself, SystemCommand::ExecOutput otherwise.
    [[nodiscard]] auto Execute(std::vector<std::string> argv,
                               std::map<std::string, std::string> env,
                               std::filesystem::path const& cwd,
                               std::filesystem::path const& tmpdir) noexcept
        -> std::optional<ExecOutput> {
        if (not FileSystemManager::IsDirectory(tmpdir)) {
            logger_.Emit(LogLevel::Error,
                         "Temporary directory does not exist {}",
                         tmpdir.string());
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
        return ExecuteCommand(cmd.data(), envp.data(), cwd, tmpdir);
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
    /// \param tmpdir   Temporary directory for storing stdout/stderr files.
    /// \returns ExecOutput if command was successfully submitted to the system.
    /// \returns std::nullopt on internal failure.
    [[nodiscard]] auto ExecuteCommand(
        char* const* cmd,
        char* const* envp,
        std::filesystem::path const& cwd,
        std::filesystem::path const& tmpdir) noexcept
        -> std::optional<ExecOutput> {
        auto stdout_file = tmpdir / "stdout";
        auto stderr_file = tmpdir / "stderr";
        if (auto const out = OpenFile(stdout_file)) {
            if (auto const err = OpenFile(stderr_file)) {
                if (auto retval = ForkAndExecute(
                        cmd, envp, cwd, fileno(out.get()), fileno(err.get()))) {
                    return ExecOutput{*retval,
                                      std::move(stdout_file),
                                      std::move(stderr_file)};
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
            // some executables require an open (possibly seekable) stdin, and
            // therefore, we use an open temporary file that does not appear
            // on the file system and will be removed automatically once the
            // descriptor is closed.
            gsl::owner<FILE*> in_file = std::tmpfile();
            auto in_fd = fileno(in_file);

            // redirect and close fds
            ::dup2(in_fd, STDIN_FILENO);
            ::dup2(out_fd, STDOUT_FILENO);
            ::dup2(err_fd, STDERR_FILENO);
            ::close(in_fd);
            ::close(out_fd);
            ::close(err_fd);

            [[maybe_unused]] auto anchor =
                FileSystemManager::ChangeDirectory(cwd);

            // execute command in child process and exit
            ::execvpe(*cmd, cmd, envp);

            // report error and terminate child process if ::execvp did not exit
            std::cerr << fmt::format("Failed to execute '{}' with error: {}",
                                     *cmd,
                                     strerror(errno))
                      << std::endl;

            System::ExitWithoutCleanup(EXIT_FAILURE);
        }

        // wait for child to finish and obtain return value
        int status{};
        ::waitpid(pid, &status, 0);
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WEXITSTATUS(status);
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
