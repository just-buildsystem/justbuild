// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include <src/other_tools/git_operations/git_repo_remote.hpp>

#include "fmt/core.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/git_utils.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/system_command.hpp"
#include "src/other_tools/git_operations/git_config_settings.hpp"

extern "C" {
#include <git2.h>
#include <git2/sys/odb_backend.h>
}

namespace {

/// \brief Basic check for libgit2 protocols we support. For all other cases, we
/// should default to shell out to git instead.
[[nodiscard]] auto IsSupported(std::string const& url) -> bool {
    // look for explicit schemes
    if (url.starts_with("git://") or url.starts_with("http://") or
        url.starts_with("https://") or url.starts_with("file://")) {
        return true;
    }
    // look for url as existing filesystem directory path
    if (FileSystemManager::IsDirectory(std::filesystem::path(url))) {
        return true;
    }
    return false;  // as default
}

struct FetchIntoODBBackend {
    git_odb_backend parent;
    git_odb* target_odb;  // the odb where the fetch objects will end up into
};

[[nodiscard]] auto fetch_backend_writepack(git_odb_writepack** _writepack,
                                           git_odb_backend* _backend,
                                           [[maybe_unused]] git_odb* odb,
                                           git_indexer_progress_cb progress_cb,
                                           void* progress_payload) -> int {
    if (_backend != nullptr) {
        auto* b = reinterpret_cast<FetchIntoODBBackend*>(_backend);  // NOLINT
        return git_odb_write_pack(
            _writepack, b->target_odb, progress_cb, progress_payload);
    }
    return GIT_ERROR;
}

[[nodiscard]] auto fetch_backend_exists(git_odb_backend* _backend,
                                        const git_oid* oid) -> int {
    if (_backend != nullptr) {
        auto* b = reinterpret_cast<FetchIntoODBBackend*>(_backend);  // NOLINT
        return git_odb_exists(b->target_odb, oid);
    }
    return GIT_ERROR;
}

void fetch_backend_free(git_odb_backend* /*_backend*/) {}

[[nodiscard]] auto CreateFetchIntoODBParent() -> git_odb_backend {
    git_odb_backend b{};
    b.version = GIT_ODB_BACKEND_VERSION;
    // only populate the functions needed
    b.writepack = &fetch_backend_writepack;  // needed for fetch
    b.exists = &fetch_backend_exists;
    b.free = &fetch_backend_free;
    return b;
}

// A backend that can be used to fetch from the remote of another repository.
auto const kFetchIntoODBParent = CreateFetchIntoODBParent();

}  // namespace

auto GitRepoRemote::Open(GitCASPtr git_cas) noexcept
    -> std::optional<GitRepoRemote> {
    auto repo = GitRepoRemote(std::move(git_cas));
    if (not repo.GetRepoRef()) {
        return std::nullopt;
    }
    return repo;
}

auto GitRepoRemote::Open(std::filesystem::path const& repo_path) noexcept
    -> std::optional<GitRepoRemote> {
    auto repo = GitRepoRemote(repo_path);
    if (not repo.GetRepoRef()) {
        return std::nullopt;
    }
    return repo;
}

GitRepoRemote::GitRepoRemote(GitCASPtr git_cas) noexcept
    : GitRepo(std::move(git_cas)) {}

GitRepoRemote::GitRepoRemote(std::filesystem::path const& repo_path) noexcept
    : GitRepo(repo_path) {}

GitRepoRemote::GitRepoRemote(GitRepo&& other) noexcept
    : GitRepo(std::move(other)) {}

GitRepoRemote::GitRepoRemote(GitRepoRemote&& other) noexcept
    : GitRepo(std::move(other)) {}

auto GitRepoRemote::operator=(GitRepoRemote&& other) noexcept
    -> GitRepoRemote& {
    GitRepo::operator=(std::move(other));
    return *this;
}

auto GitRepoRemote::InitAndOpen(std::filesystem::path const& repo_path,
                                bool is_bare) noexcept
    -> std::optional<GitRepoRemote> {
    auto res = GitRepo::InitAndOpen(repo_path, is_bare);
    if (res) {
        return GitRepoRemote(std::move(*res));
    }
    return std::nullopt;
}

auto GitRepoRemote::GetCommitFromRemote(std::shared_ptr<git_config> cfg,
                                        std::string const& repo_url,
                                        std::string const& branch,
                                        anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot update commit using a fake repository!",
                      true /*fatal*/);
            return std::nullopt;
        }
        // create remote
        git_remote* remote_ptr{nullptr};
        if (git_remote_create_anonymous(
                &remote_ptr, GetRepoRef().get(), repo_url.c_str()) != 0) {
            (*logger)(
                fmt::format("creating anonymous remote for git repository {} "
                            "failed with:\n{}",
                            GetGitPath().string(),
                            GitLastError()),
                true /*fatal*/);
            git_remote_free(remote_ptr);
            return std::nullopt;
        }
        auto remote = std::unique_ptr<git_remote, decltype(&remote_closer)>(
            remote_ptr, remote_closer);
        // get the canonical url
        auto canonical_url = std::string(git_remote_url(remote.get()));

        // get a well-defined config file
        if (not cfg) {
            // get config snapshot of current repo (shared with caller)
            cfg = GetConfigSnapshot();
            if (cfg == nullptr) {
                (*logger)(fmt::format("retrieving config object in get commit "
                                      "from remote failed with:\n{}",
                                      GitLastError()),
                          true /*fatal*/);
                return std::nullopt;
            }
        }

        // connect to remote
        git_remote_callbacks callbacks{};
        git_remote_init_callbacks(&callbacks, GIT_REMOTE_CALLBACKS_VERSION);

        // set custom SSL verification callback; use canonicalized url
        auto cert_check =
            GitConfigSettings::GetSSLCallback(cfg, canonical_url, logger);
        if (not cert_check) {
            // error occurred while handling the url
            return std::nullopt;
        }
        callbacks.certificate_check = *cert_check;

        git_proxy_options proxy_opts{};
        git_proxy_options_init(&proxy_opts, GIT_PROXY_OPTIONS_VERSION);

        // set the proxy information
        auto proxy_info =
            GitConfigSettings::GetProxySettings(cfg, canonical_url, logger);
        if (not proxy_info) {
            // error occurred while handling the url
            return std::nullopt;
        }
        if (proxy_info.value()) {
            // found proxy
            proxy_opts.type = GIT_PROXY_SPECIFIED;
            proxy_opts.url = proxy_info.value().value().c_str();
        }
        else {
            // no proxy
            proxy_opts.type = GIT_PROXY_NONE;
        }

        if (git_remote_connect(remote.get(),
                               GIT_DIRECTION_FETCH,
                               &callbacks,
                               &proxy_opts,
                               nullptr) != 0) {
            (*logger)(
                fmt::format("connecting to remote {} for git repository {} "
                            "failed with:\n{}",
                            repo_url,
                            GetGitPath().string(),
                            GitLastError()),
                true /*fatal*/);
            return std::nullopt;
        }
        // get the list of refs from remote
        // NOTE: refs will be owned by remote, so we DON'T have to free it!
        git_remote_head const** refs = nullptr;
        size_t refs_len = 0;
        if (git_remote_ls(&refs, &refs_len, remote.get()) != 0) {
            (*logger)(
                fmt::format("refs retrieval from remote {} failed with:\n{}",
                            repo_url,
                            GitLastError()),
                true /*fatal*/);
            return std::nullopt;
        }
        // figure out what remote branch the local one is tracking
        for (size_t i = 0; i < refs_len; ++i) {
            // by treating each read reference string as a path we can easily
            // check for the branch name
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            std::filesystem::path ref_name_as_path{refs[i]->name};
            if (ref_name_as_path.filename() == branch) {
                // branch found!
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                std::string new_commit_hash{git_oid_tostr_s(&refs[i]->oid)};
                return new_commit_hash;
            }
        }
        (*logger)(fmt::format("could not find branch {} for remote {}",
                              branch,
                              repo_url,
                              GitLastError()),
                  true /*fatal*/);
        return std::nullopt;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get commit from remote failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto GitRepoRemote::FetchFromRemote(std::shared_ptr<git_config> cfg,
                                    std::string const& repo_url,
                                    std::optional<std::string> const& branch,
                                    anon_logger_ptr const& logger) noexcept
    -> bool {
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot fetch commit using a fake repository!",
                      true /*fatal*/);
            return false;
        }
        // create remote from repo
        git_remote* remote_ptr{nullptr};
        if (git_remote_create_anonymous(
                &remote_ptr, GetRepoRef().get(), repo_url.c_str()) != 0) {
            (*logger)(fmt::format("creating remote {} for git repository {} "
                                  "failed with:\n{}",
                                  repo_url,
                                  GetGitPath().string(),
                                  GitLastError()),
                      true /*fatal*/);
            // cleanup resources
            git_remote_free(remote_ptr);
            return false;
        }
        // wrap remote object
        auto remote = std::unique_ptr<git_remote, decltype(&remote_closer)>(
            remote_ptr, remote_closer);
        // get the canonical url
        auto canonical_url = std::string(git_remote_url(remote.get()));

        // get a well-defined config file
        if (not cfg) {
            // get config snapshot of current repo
            git_config* cfg_ptr{nullptr};
            if (git_repository_config_snapshot(&cfg_ptr, GetRepoRef().get()) !=
                0) {
                (*logger)(fmt::format("retrieving config object in fetch from "
                                      "remote failed with:\n{}",
                                      GitLastError()),
                          true /*fatal*/);
                return false;
            }
            // share pointer with caller
            cfg.reset(cfg_ptr, config_closer);
        }

        // define default fetch options
        git_fetch_options fetch_opts{};
        git_fetch_options_init(&fetch_opts, GIT_FETCH_OPTIONS_VERSION);

        // set the proxy information
        auto proxy_info =
            GitConfigSettings::GetProxySettings(cfg, canonical_url, logger);
        if (not proxy_info) {
            // error occurred while handling the url
            return false;
        }
        if (proxy_info.value()) {
            // found proxy
            fetch_opts.proxy_opts.type = GIT_PROXY_SPECIFIED;
            fetch_opts.proxy_opts.url = proxy_info.value().value().c_str();
        }
        else {
            // no proxy
            fetch_opts.proxy_opts.type = GIT_PROXY_NONE;
        }

        // set custom SSL verification callback; use canonicalized url
        if (not cfg) {
            // get config snapshot of current repo (shared with caller)
            cfg = GetConfigSnapshot();
            if (cfg == nullptr) {
                (*logger)(fmt::format("retrieving config object in fetch from "
                                      "remote failed with:\n{}",
                                      GitLastError()),
                          true /*fatal*/);
                return false;
            }
        }
        auto cert_check =
            GitConfigSettings::GetSSLCallback(cfg, canonical_url, logger);
        if (not cert_check) {
            // error occurred while handling the url
            return false;
        }
        fetch_opts.callbacks.certificate_check = *cert_check;

        // disable update of the FETCH_HEAD pointer
        fetch_opts.update_fetchhead = 0;

        // setup fetch refspecs array
        git_strarray refspecs_array_obj{};
        if (branch) {
            // make sure we check for tags as well
            std::string tag = fmt::format("+refs/tags/{}", *branch);
            std::string head = fmt::format("+refs/heads/{}", *branch);
            PopulateStrarray(&refspecs_array_obj, {tag, head});
        }
        auto refspecs_array =
            std::unique_ptr<git_strarray, decltype(&strarray_deleter)>(
                &refspecs_array_obj, strarray_deleter);

        if (git_remote_fetch(
                remote.get(), refspecs_array.get(), &fetch_opts, nullptr) !=
            0) {
            (*logger)(
                fmt::format("fetching{} in git repository {} failed "
                            "with:\n{}",
                            branch ? fmt::format(" branch {}", *branch) : "",
                            GetGitPath().string(),
                            GitLastError()),
                true /*fatal*/);
            return false;
        }
        return true;  // success!
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "fetch from remote failed with:\n{}", ex.what());
        return false;
    }
}

auto GitRepoRemote::UpdateCommitViaTmpRepo(
    std::filesystem::path const& tmp_dir,
    std::string const& repo_url,
    std::string const& branch,
    std::string const& git_bin,
    std::vector<std::string> const& launcher,
    anon_logger_ptr const& logger) const noexcept
    -> std::optional<std::string> {
    try {
        // check for internally supported protocols
        if (IsSupported(repo_url)) {
            // preferably with a "fake" repository!
            if (not IsRepoFake()) {
                Logger::Log(LogLevel::Debug,
                            "Commit update called on a real repository");
            }
            // create the temporary real repository
            auto tmp_repo =
                GitRepoRemote::InitAndOpen(tmp_dir, /*is_bare=*/true);
            if (tmp_repo == std::nullopt) {
                return std::nullopt;
            }
            // setup wrapped logger
            auto wrapped_logger = std::make_shared<anon_logger_t>(
                [logger](auto const& msg, bool fatal) {
                    (*logger)(
                        fmt::format(
                            "While doing commit update via tmp repo:\n{}", msg),
                        fatal);
                });
            // get the config of the correct target repo
            auto cfg = GetConfigSnapshot();
            if (cfg == nullptr) {
                (*logger)(
                    fmt::format("retrieving config object in update commit "
                                "via tmp repo failed with:\n{}",
                                GitLastError()),
                    true /*fatal*/);
                return std::nullopt;
            }
            return tmp_repo->GetCommitFromRemote(
                cfg, repo_url, branch, wrapped_logger);
        }
        // default to shelling out to git for non-explicitly supported protocols
        auto cmdline = launcher;
        cmdline.insert(cmdline.end(), {git_bin, "ls-remote", repo_url, branch});
        // set up the system command
        SystemCommand system{repo_url};
        auto const command_output =
            system.Execute(cmdline,
                           {},            // default env
                           GetGitPath(),  // which path is not actually relevant
                           tmp_dir);
        // output file can be read anyway
        std::string out_str{};
        auto cmd_out = FileSystemManager::ReadFile(command_output->stdout_file);
        if (cmd_out) {
            out_str = *cmd_out;
        }
        // check for command failure
        if (not command_output) {
            std::string err_str{};
            auto cmd_err =
                FileSystemManager::ReadFile(command_output->stderr_file);

            if (cmd_err) {
                err_str = *cmd_err;
            }
            std::string output{};
            if (!out_str.empty() || !err_str.empty()) {
                output = fmt::format(" with output:\n{}{}", out_str, err_str);
            }
            (*logger)(fmt::format("List remote commits command {} failed{}",
                                  nlohmann::json(cmdline).dump(),
                                  output),
                      /*fatal=*/true);
            return std::nullopt;
        }
        // report failure to read generated output file or the file is empty
        if (out_str.empty()) {
            (*logger)(fmt::format("List remote commits command {} failed to "
                                  "produce an output",
                                  nlohmann::json(cmdline).dump()),
                      /*fatal=*/true);

            return std::nullopt;
        }
        // parse the output: it should contain two tab-separated columns,
        // with the commit being the first entry
        auto str_len = out_str.find('\t');
        if (str_len == std::string::npos) {
            (*logger)(fmt::format("List remote commits command {} produced "
                                  "malformed output:\n{}",
                                  out_str),
                      /*fatal=*/true);
            return std::nullopt;
        }
        // success!
        return out_str.substr(0, str_len);
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Update commit failed with:\n{}", ex.what());
        return std::nullopt;
    }
}

auto GitRepoRemote::FetchViaTmpRepo(std::filesystem::path const& tmp_dir,
                                    std::string const& repo_url,
                                    std::optional<std::string> const& branch,
                                    std::string const& git_bin,
                                    std::vector<std::string> const& launcher,
                                    anon_logger_ptr const& logger) noexcept
    -> bool {
    try {
        // check for internally supported protocols
        if (IsSupported(repo_url)) {
            // preferably with a "fake" repository!
            if (not IsRepoFake()) {
                Logger::Log(LogLevel::Debug,
                            "Branch fetch called on a real repository");
            }
            // create the temporary real repository
            // it can be bare, as the refspecs for this fetch will be given
            // explicitly.
            auto tmp_repo =
                GitRepoRemote::InitAndOpen(tmp_dir, /*is_bare=*/true);
            if (tmp_repo == std::nullopt) {
                return false;
            }
            // add backend, with max priority
            FetchIntoODBBackend b{kFetchIntoODBParent, GetGitOdb().get()};
            if (git_odb_add_backend(
                    tmp_repo->GetGitOdb().get(),
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                    reinterpret_cast<git_odb_backend*>(&b),
                    std::numeric_limits<int>::max()) == 0) {
                // setup wrapped logger
                auto wrapped_logger = std::make_shared<anon_logger_t>(
                    [logger](auto const& msg, bool fatal) {
                        (*logger)(fmt::format("While doing branch fetch via "
                                              "tmp repo:\n{}",
                                              msg),
                                  fatal);
                    });
                // get the config of the correct target repo
                auto cfg = GetConfigSnapshot();
                if (cfg == nullptr) {
                    (*logger)(
                        fmt::format("retrieving config object in fetch via "
                                    "tmp repo failed with:\n{}",
                                    GitLastError()),
                        true /*fatal*/);
                    return false;
                }
                return tmp_repo->FetchFromRemote(
                    cfg, repo_url, branch, wrapped_logger);
            }
            return false;
        }
        // default to shelling out to git for non-explicitly supported protocols
        auto cmdline = launcher;
        // Note: Because we fetch with URL, not a known remote, no refs are
        // being updated by default, so git has no reason to get a lock
        // file. This however does not imply automatically that fetches
        // might not internally wait for each other through other means.
        cmdline.insert(cmdline.end(),
                       {git_bin,
                        "fetch",
                        "--no-auto-gc",
                        "--no-write-fetch-head",
                        repo_url});
        if (branch) {
            cmdline.push_back(*branch);
        }
        // run command
        SystemCommand system{repo_url};
        auto const command_output = system.Execute(cmdline,
                                                   {},  // caller env
                                                   GetGitPath(),
                                                   tmp_dir);
        if (not command_output) {
            std::string out_str{};
            std::string err_str{};
            auto cmd_out =
                FileSystemManager::ReadFile(command_output->stdout_file);
            auto cmd_err =
                FileSystemManager::ReadFile(command_output->stderr_file);
            if (cmd_out) {
                out_str = *cmd_out;
            }
            if (cmd_err) {
                err_str = *cmd_err;
            }
            std::string output{};
            if (!out_str.empty() || !err_str.empty()) {
                output = fmt::format(" with output:\n{}{}", out_str, err_str);
            }
            (*logger)(fmt::format("Fetch command {} failed{}",
                                  nlohmann::json(cmdline).dump(),
                                  output),
                      /*fatal=*/true);
            return false;
        }
        return true;  // fetch successful
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "Fetch failed with:\n{}", ex.what());
        return false;
    }
}

auto GitRepoRemote::GetConfigSnapshot() const -> std::shared_ptr<git_config> {
    git_config* cfg_ptr{nullptr};
    if (git_repository_config_snapshot(&cfg_ptr, GetRepoRef().get()) != 0) {
        return nullptr;
    }
    return std::shared_ptr<git_config>(cfg_ptr, config_closer);
}
