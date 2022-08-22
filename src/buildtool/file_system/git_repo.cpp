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

#include <src/buildtool/file_system/git_repo.hpp>

#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/path.hpp"

extern "C" {
#include <git2.h>
}

namespace {

constexpr std::size_t kWaitTime{2};  // time in ms between tries for git locks

[[nodiscard]] auto GitLastError() noexcept -> std::string {
    git_error const* err{nullptr};
    if ((err = git_error_last()) != nullptr and err->message != nullptr) {
        return fmt::format("error code {}: {}", err->klass, err->message);
    }
    return "<unknown error>";
}

}  // namespace

auto GitRepo::Open(GitCASPtr git_cas) noexcept -> std::optional<GitRepo> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    auto repo = GitRepo(std::move(git_cas));
    if (repo.repo_ == nullptr) {
        return std::nullopt;
    }
    return repo;
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::Open(std::filesystem::path const& repo_path) noexcept
    -> std::optional<GitRepo> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    auto repo = GitRepo(repo_path);
    if (repo.repo_ == nullptr) {
        return std::nullopt;
    }
    return repo;
#endif  // BOOTSTRAP_BUILD_TOOL
}

GitRepo::GitRepo(GitCASPtr git_cas) noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (git_cas != nullptr) {
        if (git_repository_wrap_odb(&repo_, git_cas->odb_) != 0) {
            Logger::Log(LogLevel::Error,
                        "could not create wrapper for git repository");
            git_repository_free(repo_);
            repo_ = nullptr;
            return;
        }
        is_repo_fake_ = true;
        git_cas_ = std::move(git_cas);
    }
    else {
        Logger::Log(LogLevel::Error,
                    "git repository creation attempted with null odb!");
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

GitRepo::GitRepo(std::filesystem::path const& repo_path) noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        static std::mutex repo_mutex{};
        std::unique_lock lock{repo_mutex};
        auto cas = std::make_shared<GitCAS>();
        // open repo, but retain it
        if (git_repository_open(&repo_, repo_path.c_str()) != 0) {
            Logger::Log(LogLevel::Error,
                        "opening git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            git_repository_free(repo_);
            repo_ = nullptr;
            return;
        }
        // get odb
        git_repository_odb(&cas->odb_, repo_);
        if (cas->odb_ == nullptr) {
            Logger::Log(LogLevel::Error,
                        "retrieving odb of git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            git_repository_free(repo_);
            repo_ = nullptr;
            return;
        }
        is_repo_fake_ = false;
        // save root path
        cas->git_path_ = ToNormalPath(std::filesystem::absolute(
            std::filesystem::path(git_repository_path(repo_))));
        // retain the pointer
        git_cas_ = std::static_pointer_cast<GitCAS const>(cas);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "opening git object database failed with:\n{}",
                    ex.what());
        repo_ = nullptr;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

GitRepo::GitRepo(GitRepo&& other) noexcept
    : git_cas_{std::move(other.git_cas_)},
      repo_{other.repo_},
      is_repo_fake_{other.is_repo_fake_} {
    other.repo_ = nullptr;
}

auto GitRepo::operator=(GitRepo&& other) noexcept -> GitRepo& {
    git_cas_ = std::move(other.git_cas_);
    repo_ = other.repo_;
    is_repo_fake_ = other.is_repo_fake_;
    other.git_cas_ = nullptr;
    return *this;
}

auto GitRepo::InitAndOpen(std::filesystem::path const& repo_path,
                          bool is_bare) noexcept -> std::optional<GitRepo> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        static std::mutex repo_mutex{};
        std::unique_lock lock{repo_mutex};

        auto git_state = GitContext();  // initialize libgit2

        git_repository* tmp_repo{nullptr};
        size_t max_attempts = 3;  // number of tries
        int err = 0;
        while (max_attempts > 0) {
            --max_attempts;
            err = git_repository_init(
                &tmp_repo, repo_path.c_str(), static_cast<size_t>(is_bare));
            if (err == 0) {
                git_repository_free(tmp_repo);
                return GitRepo(repo_path);  // success
            }
            git_repository_free(tmp_repo);  // cleanup before next attempt
            // check if init hasn't already happened in another process
            if (git_repository_open_ext(nullptr,
                                        repo_path.c_str(),
                                        GIT_REPOSITORY_OPEN_NO_SEARCH,
                                        nullptr) == 0) {
                return GitRepo(repo_path);  // success
            }
            // repo still not created, so sleep and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(kWaitTime));
        }
        Logger::Log(
            LogLevel::Error,
            "initializing git repository {} failed with error code:\n{}",
            (repo_path / "").string(),
            err);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "initializing git repository {} failed with:\n{}",
                    (repo_path / "").string(),
                    ex.what());
    }
#endif  // BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
}

auto GitRepo::GetGitCAS() const noexcept -> GitCASPtr {
    return git_cas_;
}

GitRepo::~GitRepo() noexcept {
    // release resources
    git_repository_free(repo_);
}

auto GitRepo::IsRepoFake() const noexcept -> bool {
    return is_repo_fake_;
}
