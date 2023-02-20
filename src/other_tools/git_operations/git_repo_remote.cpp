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
#include "src/buildtool/file_system/git_utils.hpp"
#include "src/buildtool/logging/logger.hpp"

extern "C" {
#include <git2.h>
#include <git2/sys/odb_backend.h>
}

namespace {

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

// callback to enable SSL certificate check for remote fetch
const auto certificate_check_cb = [](git_cert* /*cert*/,
                                     int /*valid*/,
                                     const char* /*host*/,
                                     void* /*payload*/) -> int { return 1; };

// callback to remote fetch without an SSL certificate check
const auto certificate_passthrough_cb = [](git_cert* /*cert*/,
                                           int /*valid*/,
                                           const char* /*host*/,
                                           void* /*payload*/) -> int {
    return 0;
};

/// \brief Set a custom SSL certificate check callback to honor the existing Git
/// configuration of a repository trying to connect to a remote.
[[nodiscard]] auto SetCustomSSLCertificateCheckCallback(git_repository* repo)
    -> git_transport_certificate_check_cb {
    // check SSL verification settings, from most to least specific
    std::optional<int> check_cert{std::nullopt};
    // check gitconfig; ignore errors
    git_config* cfg{nullptr};
    int tmp{};
    if (git_repository_config(&cfg, repo) == 0 and
        git_config_get_bool(&tmp, cfg, "http.sslVerify") == 0) {
        check_cert = tmp;
    }
    if (not check_cert) {
        // check for GIT_SSL_NO_VERIFY environment variable
        const char* ssl_no_verify_var{std::getenv("GIT_SSL_NO_VERIFY")};
        if (ssl_no_verify_var != nullptr and
            git_config_parse_bool(&tmp, ssl_no_verify_var) == 0) {
            check_cert = tmp;
        }
    }
    // cleanup memory
    git_config_free(cfg);
    // set callback
    return (check_cert and check_cert.value() == 0) ? certificate_passthrough_cb
                                                    : certificate_check_cb;
}

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

auto GitRepoRemote::GetCommitFromRemote(std::string const& repo_url,
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

        // connect to remote
        git_remote_callbacks callbacks{};
        git_remote_init_callbacks(&callbacks, GIT_REMOTE_CALLBACKS_VERSION);

        // set custom SSL verification callback
        callbacks.certificate_check =
            SetCustomSSLCertificateCheckCallback(GetRepoRef().get());

        git_proxy_options proxy_opts{};
        git_proxy_options_init(&proxy_opts, GIT_PROXY_OPTIONS_VERSION);

        // set the option to auto-detect proxy settings
        proxy_opts.type = GIT_PROXY_AUTO;

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

auto GitRepoRemote::FetchFromRemote(std::string const& repo_url,
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

        // define default fetch options
        git_fetch_options fetch_opts{};
        git_fetch_options_init(&fetch_opts, GIT_FETCH_OPTIONS_VERSION);

        // set the option to auto-detect proxy settings
        fetch_opts.proxy_opts.type = GIT_PROXY_AUTO;

        // set custom SSL verification callback
        fetch_opts.callbacks.certificate_check =
            SetCustomSSLCertificateCheckCallback(GetRepoRef().get());

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
    std::filesystem::path const& tmp_repo_path,
    std::string const& repo_url,
    std::string const& branch,
    anon_logger_ptr const& logger) const noexcept
    -> std::optional<std::string> {
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            (*logger)("WARNING: commit update called on a real repository!\n",
                      false /*fatal*/);
        }
        // create the temporary real repository
        auto tmp_repo =
            GitRepoRemote::InitAndOpen(tmp_repo_path, /*is_bare=*/true);
        if (tmp_repo == std::nullopt) {
            return std::nullopt;
        }
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<anon_logger_t>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While doing commit update via tmp repo:\n{}",
                                msg),
                    fatal);
            });
        return tmp_repo->GetCommitFromRemote(repo_url, branch, wrapped_logger);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "update commit via tmp repo failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
}

auto GitRepoRemote::FetchViaTmpRepo(std::filesystem::path const& tmp_repo_path,
                                    std::string const& repo_url,
                                    std::optional<std::string> const& branch,
                                    anon_logger_ptr const& logger) noexcept
    -> bool {
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            (*logger)("WARNING: branch fetch called on a real repository!\n",
                      false /*fatal*/);
        }
        // create the temporary real repository
        // it can be bare, as the refspecs for this fetch will be given
        // explicitly.
        auto tmp_repo =
            GitRepoRemote::InitAndOpen(tmp_repo_path, /*is_bare=*/true);
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
                    (*logger)(
                        fmt::format(
                            "While doing branch fetch via tmp repo:\n{}", msg),
                        fatal);
                });
            return tmp_repo->FetchFromRemote(repo_url, branch, wrapped_logger);
        }
        return false;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "fetch via tmp repo failed with:\n{}", ex.what());
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
