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

#include "src/buildtool/file_system/git_repo.hpp"

#include <thread>
#include <unordered_set>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/gsl.hpp"
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"

extern "C" {
#include <git2.h>
#include <git2/sys/odb_backend.h>
}

#ifndef BOOTSTRAP_BUILD_TOOL
namespace {

/// \brief libgit2 file modes corresponding to non-special entries.
std::unordered_set<git_filemode_t> const kNonSpecialGitFileModes{
    GIT_FILEMODE_BLOB,
    GIT_FILEMODE_BLOB_EXECUTABLE,
    GIT_FILEMODE_TREE};

[[nodiscard]] auto ToHexString(git_oid const& oid) noexcept
    -> std::optional<std::string> {
    std::string hex_id(GIT_OID_HEXSZ, '\0');
    if (git_oid_fmt(hex_id.data(), &oid) != 0) {
        return std::nullopt;
    }
    return hex_id;
}

[[nodiscard]] auto ToRawString(git_oid const& oid) noexcept
    -> std::optional<std::string> {
    if (auto hex_id = ToHexString(oid)) {
        return FromHexString(*hex_id);
    }
    return std::nullopt;
}

/// \brief Returns true if mode corresponds to a supported object type.
[[nodiscard]] auto GitFileModeIsNonSpecial(git_filemode_t const& mode) noexcept
    -> bool {
    return kNonSpecialGitFileModes.contains(mode);
}

[[nodiscard]] auto GitFileModeToObjectType(git_filemode_t const& mode) noexcept
    -> std::optional<ObjectType> {
    switch (mode) {
        case GIT_FILEMODE_BLOB:
            return ObjectType::File;
        case GIT_FILEMODE_BLOB_EXECUTABLE:
            return ObjectType::Executable;
        case GIT_FILEMODE_TREE:
            return ObjectType::Tree;
        case GIT_FILEMODE_LINK:
            return ObjectType::Symlink;  // condition not tested here
        default: {
            std::ostringstream str;
            str << std::oct << static_cast<int>(mode);
            Logger::Log(
                LogLevel::Error, "unsupported git filemode {}", str.str());
            return std::nullopt;
        }
    }
}

[[nodiscard]] constexpr auto ObjectTypeToGitFileMode(ObjectType type) noexcept
    -> git_filemode_t {
    switch (type) {
        case ObjectType::File:
            return GIT_FILEMODE_BLOB;
        case ObjectType::Executable:
            return GIT_FILEMODE_BLOB_EXECUTABLE;
        case ObjectType::Tree:
            return GIT_FILEMODE_TREE;
        case ObjectType::Symlink:
            return GIT_FILEMODE_LINK;
    }
    return GIT_FILEMODE_UNREADABLE;  // make gcc happy
}

[[nodiscard]] auto GitTypeToObjectType(git_object_t const& type) noexcept
    -> std::optional<ObjectType> {
    switch (type) {
        case GIT_OBJECT_BLOB:
            return ObjectType::File;
        case GIT_OBJECT_TREE:
            return ObjectType::Tree;
        default:
            Logger::Log(LogLevel::Error,
                        "unsupported git object type {}",
                        git_object_type2string(type));
            return std::nullopt;
    }
}

#ifndef NDEBUG
[[nodiscard]] auto ValidateEntries(GitRepo::tree_entries_t const& entries)
    -> bool {
    return std::all_of(entries.begin(), entries.end(), [](auto entry) {
        auto const& [id, nodes] = entry;
        // for a given raw id, either all entries are trees or none of them
        return std::all_of(
                   nodes.begin(),
                   nodes.end(),
                   [](auto entry) { return IsTreeObject(entry.type); }) or
               std::none_of(nodes.begin(), nodes.end(), [](auto entry) {
                   return IsTreeObject(entry.type);
               });
    });
}
#endif

[[nodiscard]] auto flat_tree_walker_ignore_special(const char* /*root*/,
                                                   const git_tree_entry* entry,
                                                   void* payload) noexcept
    -> int {
    auto* entries =
        reinterpret_cast<GitRepo::tree_entries_t*>(payload);  // NOLINT

    std::string name = git_tree_entry_name(entry);
    auto const* oid = git_tree_entry_id(entry);
    if (auto raw_id = ToRawString(*oid)) {
        if (not GitFileModeIsNonSpecial(git_tree_entry_filemode(entry))) {
            return 0;  // allow, but not store
        }
        if (auto type =
                GitFileModeToObjectType(git_tree_entry_filemode(entry))) {
            // no need to test for symlinks, as no symlink entry will reach this
            (*entries)[*raw_id].emplace_back(std::move(name), *type);
            return 1;  // return >=0 on success, 1 == skip subtrees (flat)
        }
    }
    Logger::Log(LogLevel::Error, "failed walk for git tree entry: {}", name);
    return -1;  // fail
}

[[nodiscard]] auto flat_tree_walker(const char* /*root*/,
                                    const git_tree_entry* entry,
                                    void* payload) noexcept -> int {
    auto* entries =
        reinterpret_cast<GitRepo::tree_entries_t*>(payload);  // NOLINT

    std::string name = git_tree_entry_name(entry);
    auto const* oid = git_tree_entry_id(entry);
    if (auto raw_id = ToRawString(*oid)) {
        if (auto type =
                GitFileModeToObjectType(git_tree_entry_filemode(entry))) {
            // symlinks need to be checked in caller for non-upwardness
            (*entries)[*raw_id].emplace_back(std::move(name), *type);
            return 1;  // return >=0 on success, 1 == skip subtrees (flat)
        }
    }
    Logger::Log(LogLevel::Error, "failed walk for git tree entry: {}", name);
    return -1;  // fail
}

struct InMemoryODBBackend {
    git_odb_backend parent;
    GitRepo::tree_entries_t const* entries{nullptr};       // object headers
    std::unordered_map<std::string, std::string> trees{};  // solid tree objects
};

[[nodiscard]] auto backend_read_header(size_t* len_p,
                                       git_object_t* type_p,
                                       git_odb_backend* _backend,
                                       const git_oid* oid) -> int {
    if (len_p != nullptr and type_p != nullptr and _backend != nullptr and
        oid != nullptr) {
        auto* b = reinterpret_cast<InMemoryODBBackend*>(_backend);  // NOLINT
        if (auto id = ToRawString(*oid)) {
            if (auto it = b->trees.find(*id); it != b->trees.end()) {
                *type_p = GIT_OBJECT_TREE;
                *len_p = it->second.size();
                return GIT_OK;
            }
            if (b->entries != nullptr) {
                if (auto it = b->entries->find(*id); it != b->entries->end()) {
                    if (not it->second.empty()) {
                        // pretend object is in database, size is ignored.
                        *type_p = IsTreeObject(it->second.front().type)
                                      ? GIT_OBJECT_TREE
                                      : GIT_OBJECT_BLOB;
                        *len_p = 0;
                        return GIT_OK;
                    }
                }
            }
            return GIT_ENOTFOUND;
        }
    }
    return GIT_ERROR;
}

[[nodiscard]] auto backend_read(void** data_p,
                                size_t* len_p,
                                git_object_t* type_p,
                                git_odb_backend* _backend,
                                const git_oid* oid) -> int {
    if (data_p != nullptr and len_p != nullptr and type_p != nullptr and
        _backend != nullptr and oid != nullptr) {
        auto* b = reinterpret_cast<InMemoryODBBackend*>(_backend);  // NOLINT
        if (auto id = ToRawString(*oid)) {
            if (auto it = b->trees.find(*id); it != b->trees.end()) {
                *type_p = GIT_OBJECT_TREE;
                *len_p = it->second.size();
                *data_p = git_odb_backend_data_alloc(_backend, *len_p);
                if (*data_p == nullptr) {
                    return GIT_ERROR;
                }
                std::memcpy(*data_p, it->second.data(), *len_p);
                return GIT_OK;
            }
            return GIT_ENOTFOUND;
        }
    }
    return GIT_ERROR;
}

[[nodiscard]] auto backend_exists(git_odb_backend* _backend, const git_oid* oid)
    -> int {
    if (_backend != nullptr and oid != nullptr) {
        auto* b = reinterpret_cast<InMemoryODBBackend*>(_backend);  // NOLINT
        if (auto id = ToRawString(*oid)) {
            return (b->entries != nullptr and b->entries->contains(*id)) or
                           b->trees.contains(*id)
                       ? 1
                       : 0;
        }
    }
    return GIT_ERROR;
}

[[nodiscard]] auto backend_write(git_odb_backend* _backend,
                                 const git_oid* oid,
                                 const void* data,
                                 size_t len,
                                 git_object_t type) -> int {
    if (data != nullptr and _backend != nullptr and oid != nullptr) {
        auto* b = reinterpret_cast<InMemoryODBBackend*>(_backend);  // NOLINT
        if (auto id = ToRawString(*oid)) {
            if (auto t = GitTypeToObjectType(type)) {
                std::string s(static_cast<char const*>(data), len);
                if (type == GIT_OBJECT_TREE) {
                    b->trees.emplace(std::move(*id), std::move(s));
                    return GIT_OK;
                }
            }
        }
    }
    return GIT_ERROR;
}

void backend_free(git_odb_backend* /*_backend*/) {}

[[nodiscard]] auto CreateInMemoryODBParent() -> git_odb_backend {
    git_odb_backend b{};
    b.version = GIT_ODB_BACKEND_VERSION;
    b.read_header = &backend_read_header;
    b.read = &backend_read;
    b.exists = &backend_exists;
    b.write = &backend_write;
    b.free = &backend_free;
    return b;
}

// A backend that can be used to read and create tree objects in-memory.
auto const kInMemoryODBParent = CreateInMemoryODBParent();

}  // namespace
#endif  // BOOTSTRAP_BUILD_TOOL

GitRepo::GuardedRepo::GuardedRepo(std::shared_mutex* mutex) noexcept
    : mutex_{mutex} {}

auto GitRepo::GuardedRepo::Ptr() -> git_repository* {
    return repo_;
}

auto GitRepo::GuardedRepo::PtrRef() -> git_repository** {
    return &repo_;
}

GitRepo::GuardedRepo::~GuardedRepo() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (repo_ != nullptr) {
        std::unique_lock lock{*mutex_};
        git_repository_free(repo_);
    }
#endif
}

auto GitRepo::Open(GitCASPtr git_cas) noexcept -> std::optional<GitRepo> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    auto repo = GitRepo(std::move(git_cas));
    if (not repo.repo_) {
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
    if (not repo.repo_) {
        return std::nullopt;
    }
    return repo;
#endif  // BOOTSTRAP_BUILD_TOOL
}

GitRepo::GitRepo(GitCASPtr git_cas) noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        if (git_cas != nullptr) {
            auto repo_ptr = std::make_shared<GuardedRepo>(&git_cas->mutex_);
            {
                // acquire the odb lock exclusively
                std::unique_lock lock{git_cas->mutex_};
                if (git_repository_wrap_odb(repo_ptr->PtrRef(),
                                            git_cas->odb_.get()) != 0) {
                    Logger::Log(LogLevel::Error,
                                "could not create wrapper for git repository");
                    return;
                }
            }
            repo_ = std::move(repo_ptr);
            is_repo_fake_ = true;
            git_cas_ = std::move(git_cas);
        }
        else {
            Logger::Log(LogLevel::Error,
                        "git repository creation attempted with null odb!");
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "opening git object database failed with:\n{}",
                    ex.what());
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
        auto repo_ptr = std::make_shared<GuardedRepo>(&cas->mutex_);
        if (git_repository_open_ext(repo_ptr->PtrRef(),
                                    repo_path.c_str(),
                                    GIT_REPOSITORY_OPEN_NO_SEARCH,
                                    nullptr) != 0) {
            Logger::Log(LogLevel::Error,
                        "opening git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            return;
        }
        repo_ = repo_ptr;  // retain repo pointer
        // get odb
        git_odb* odb_ptr{nullptr};
        git_repository_odb(&odb_ptr, repo_->Ptr());
        if (odb_ptr == nullptr) {
            Logger::Log(LogLevel::Error,
                        "retrieving odb of git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            // release resources
            git_odb_free(odb_ptr);
            return;
        }
        cas->odb_.reset(odb_ptr);  // retain odb pointer
        is_repo_fake_ = false;
        // save root path; this differs if repository is bare or not
        if (git_repository_is_bare(repo_->Ptr()) != 0) {
            cas->git_path_ = std::filesystem::absolute(
                ToNormalPath(git_repository_path(repo_->Ptr())));
        }
        else {
            cas->git_path_ = std::filesystem::absolute(
                ToNormalPath(git_repository_workdir(repo_->Ptr())));
        }
        // retain the pointer
        git_cas_ = std::static_pointer_cast<GitCAS const>(cas);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "opening git object database failed with:\n{}",
                    ex.what());
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

GitRepo::GitRepo(GitRepo&& other) noexcept
    : git_cas_{std::move(other.git_cas_)},
      repo_{std::move(other.repo_)},
      is_repo_fake_{other.is_repo_fake_} {
    other.git_cas_ = nullptr;
}

auto GitRepo::operator=(GitRepo&& other) noexcept -> GitRepo& {
    try {
        git_cas_ = std::move(other.git_cas_);
        repo_ = std::move(other.repo_);
        is_repo_fake_ = other.is_repo_fake_;
        other.git_cas_ = nullptr;
    } catch (...) {
    }
    return *this;
}

auto GitRepo::InitAndOpen(std::filesystem::path const& repo_path,
                          bool is_bare) noexcept -> std::optional<GitRepo> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        static std::mutex repo_mutex{};
        std::unique_lock lock{repo_mutex};

        GitContext::Create();  // initialize libgit2

        // check if init is actually needed
        if (git_repository_open_ext(nullptr,
                                    repo_path.c_str(),
                                    GIT_REPOSITORY_OPEN_NO_SEARCH,
                                    nullptr) == 0) {
            return GitRepo(repo_path);  // success
        }

        git_repository* tmp_repo{nullptr};
        size_t max_attempts = kGitLockNumTries;  // number of tries
        int err = 0;
        std::string err_mess{};
        while (max_attempts > 0) {
            --max_attempts;
            err = git_repository_init(
                &tmp_repo, repo_path.c_str(), static_cast<size_t>(is_bare));
            if (err == 0) {
                git_repository_free(tmp_repo);
                return GitRepo(repo_path);  // success
            }
            err_mess = GitLastError();  // store last error message
            // only retry if failure is due to locking
            if (err != GIT_ELOCKED) {
                break;
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
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kGitLockWaitTime));
        }
        Logger::Log(LogLevel::Error,
                    "initializing git repository {} failed with:\n{}",
                    (repo_path / "").string(),
                    err_mess);
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

auto GitRepo::GetRepoRef() const noexcept -> GuardedRepoPtr {
    return repo_;
}

auto GitRepo::GetGitPath() const noexcept -> std::filesystem::path const& {
    return git_cas_->git_path_;
}

auto GitRepo::GetGitOdb() const noexcept
    -> std::unique_ptr<git_odb, decltype(&odb_closer)> const& {
    return git_cas_->odb_;
}

auto GitRepo::StageAndCommitAllAnonymous(std::string const& message,
                                         anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot stage and commit files using a fake repository!",
                      true /*fatal*/);
            return std::nullopt;
        }
        // share the odb lock
        std::shared_lock lock{GetGitCAS()->mutex_};

        // cannot perform this operation on a bare repository; this has to be
        // checked because git_index_add_bypath will not do it for us!
        if (not FileSystemManager::Exists(GetGitCAS()->git_path_ / ".git")) {
            (*logger)("cannot stage and commit files in a bare repository!",
                      true /*fatal*/);
            return std::nullopt;
        }

        // add all files to be staged
        git_index* index_ptr{nullptr};
        git_repository_index(&index_ptr, repo_->Ptr());
        auto index = std::unique_ptr<git_index, decltype(&index_closer)>(
            index_ptr, index_closer);

        // due to mismanagement of .gitignore rules by libgit2 when doing a
        // forced add all, we resort to using git_index_add_bypath manually for
        // all entries, instead of git_index_add_all with GIT_INDEX_ADD_FORCE.
        auto use_entry = [&index](std::filesystem::path const& name,
                                  bool is_tree) {
            return is_tree or
                   git_index_add_bypath(index.get(), name.c_str()) == 0;
        };
        if (not FileSystemManager::ReadDirectoryEntriesRecursive(
                GetGitCAS()->git_path_,
                use_entry,
                /*ignored_subdirs=*/{".git"})) {
            (*logger)(fmt::format(
                          "staging files in git repository {} failed with:\n{}",
                          GetGitCAS()->git_path_.string(),
                          GitLastError()),
                      true /*fatal*/);
            return std::nullopt;
        }
        // build tree from staged files
        git_oid tree_oid;
        if (git_index_write_tree(&tree_oid, index.get()) != 0) {
            (*logger)(fmt::format("building tree from index in git repository "
                                  "{} failed with:\n{}",
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            return std::nullopt;
        }

        // set committer signature
        git_signature* signature_ptr{nullptr};
        if (git_signature_new(
                &signature_ptr, "Nobody", "nobody@example.org", 0, 0) != 0) {
            (*logger)(
                fmt::format("creating signature in git repository {} failed "
                            "with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            // cleanup resources
            git_signature_free(signature_ptr);
            return std::nullopt;
        }
        auto signature =
            std::unique_ptr<git_signature, decltype(&signature_closer)>(
                signature_ptr, signature_closer);

        // get tree object
        git_tree* tree_ptr = nullptr;
        if (git_tree_lookup(&tree_ptr, repo_->Ptr(), &tree_oid) != 0) {
            (*logger)(
                fmt::format("tree lookup in git repository {} failed with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            // cleanup resources
            git_tree_free(tree_ptr);
            return std::nullopt;
        }
        auto tree = std::unique_ptr<git_tree, decltype(&tree_closer)>(
            tree_ptr, tree_closer);

        // commit the tree containing the staged files
        git_buf buffer = GIT_BUF_INIT_CONST(NULL, 0);
        git_message_prettify(&buffer, message.c_str(), 0, '#');

        git_oid commit_oid;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        if (git_commit_create_v(&commit_oid,
                                repo_->Ptr(),
                                "HEAD",
                                signature.get(),
                                signature.get(),
                                nullptr,
                                buffer.ptr,
                                tree.get(),
                                0) != 0) {
            (*logger)(
                fmt::format("git commit in repository {} failed with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            git_buf_dispose(&buffer);
            return std::nullopt;
        }
        std::string commit_hash{git_oid_tostr_s(&commit_oid)};
        git_buf_dispose(&buffer);
        return commit_hash;  // success!
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "stage and commit all failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::KeepTag(std::string const& commit,
                      std::string const& message,
                      anon_logger_ptr const& logger) noexcept -> bool {
#ifdef BOOTSTRAP_BUILD_TOOL
    return false;
#else
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot tag commits using a fake repository!",
                      true /*fatal*/);
            return false;
        }
        // share the odb lock
        std::shared_lock lock{GetGitCAS()->mutex_};

        // get commit spec
        git_object* target_ptr{nullptr};
        if (git_revparse_single(&target_ptr, repo_->Ptr(), commit.c_str()) !=
            0) {
            (*logger)(fmt::format("rev-parse commit {} in repository {} failed "
                                  "with:\n{}",
                                  commit,
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            git_object_free(target_ptr);
            return false;
        }
        auto target = std::unique_ptr<git_object, decltype(&object_closer)>(
            target_ptr, object_closer);

        // set tagger signature
        git_signature* tagger_ptr{nullptr};
        if (git_signature_new(
                &tagger_ptr, "Nobody", "nobody@example.org", 0, 0) != 0) {
            (*logger)(
                fmt::format("creating signature in git repository {} failed "
                            "with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            // cleanup resources
            git_signature_free(tagger_ptr);
            return false;
        }
        auto tagger =
            std::unique_ptr<git_signature, decltype(&signature_closer)>(
                tagger_ptr, signature_closer);

        // create tag
        git_oid oid;
        auto name = fmt::format("keep-{}", commit);
        git_strarray tag_names{};

        // check if tag hasn't already been added by another process
        if (git_tag_list_match(&tag_names, name.c_str(), repo_->Ptr()) == 0 and
            tag_names.count > 0) {
            git_strarray_dispose(&tag_names);
            return true;  // success!
        }
        git_strarray_dispose(&tag_names);  // free any allocated unused space

        size_t max_attempts = kGitLockNumTries;  // number of tries
        int err = 0;
        std::string err_mess{};
        while (max_attempts > 0) {
            --max_attempts;
            err = git_tag_create(&oid,
                                 repo_->Ptr(),
                                 name.c_str(),
                                 target.get(),
                                 tagger.get(),
                                 message.c_str(),
                                 1 /*force*/);
            if (err == 0) {
                return true;  // success!
            }
            err_mess = GitLastError();  // store last error message
            // only retry if failure is due to locking
            if (err != GIT_ELOCKED) {
                break;
            }
            // check if tag hasn't already been added by another process
            if (git_tag_list_match(&tag_names, name.c_str(), repo_->Ptr()) ==
                    0 and
                tag_names.count > 0) {
                git_strarray_dispose(&tag_names);
                return true;  // success!
            }
            git_strarray_dispose(
                &tag_names);  // free any allocated unused space
            // tag still not in, so sleep and try again
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kGitLockWaitTime));
        }
        (*logger)(
            fmt::format("tag creation in git repository {} failed with:\n{}",
                        GetGitCAS()->git_path_.string(),
                        err_mess),
            true /*fatal*/);
        return false;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "keep tag failed with:\n{}", ex.what());
        return false;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetHeadCommit(anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot access HEAD ref using a fake repository!",
                      true /*fatal*/);
            return std::nullopt;
        }
        // share the odb lock
        std::shared_lock lock{GetGitCAS()->mutex_};

        // get root commit id
        git_oid head_oid;
        if (git_reference_name_to_id(&head_oid, repo_->Ptr(), "HEAD") != 0) {
            (*logger)(fmt::format("retrieving head commit in git repository {} "
                                  "failed with:\n{}",
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            return std::nullopt;
        }
        return std::string(git_oid_tostr_s(&head_oid));
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "get head commit failed with:\n{}", ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetSubtreeFromCommit(std::string const& commit,
                                   std::string const& subdir,
                                   anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            Logger::Log(
                LogLevel::Debug,
                "Subtree id retrieval from commit called on a real repository");
        }
        // share the odb lock
        std::shared_lock lock{GetGitCAS()->mutex_};

        // get commit object
        git_oid commit_oid;
        if (git_oid_fromstr(&commit_oid, commit.c_str()) != 0) {
            (*logger)(
                fmt::format("commit ID parsing in git repository {} failed "
                            "with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            return std::nullopt;
        }

        git_commit* commit_ptr{nullptr};
        if (git_commit_lookup(&commit_ptr, repo_->Ptr(), &commit_oid) != 0) {
            (*logger)(fmt::format("retrieving commit {} in git repository {} "
                                  "failed with:\n{}",
                                  commit,
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            // cleanup resources
            git_commit_free(commit_ptr);
            return std::nullopt;
        }
        auto commit_obj = std::unique_ptr<git_commit, decltype(&commit_closer)>(
            commit_ptr, commit_closer);

        // get tree of commit
        git_tree* tree_ptr{nullptr};
        if (git_commit_tree(&tree_ptr, commit_obj.get()) != 0) {
            (*logger)(fmt::format(
                          "retrieving tree for commit {} in git repository {} "
                          "failed with:\n{}",
                          commit,
                          GetGitCAS()->git_path_.string(),
                          GitLastError()),
                      true /*fatal*/);
            // cleanup resources
            git_tree_free(tree_ptr);
            return std::nullopt;
        }
        auto tree = std::unique_ptr<git_tree, decltype(&tree_closer)>(
            tree_ptr, tree_closer);

        if (subdir != ".") {
            // get hash for actual subdir
            git_tree_entry* subtree_entry_ptr{nullptr};
            if (git_tree_entry_bypath(
                    &subtree_entry_ptr, tree.get(), subdir.c_str()) != 0) {
                (*logger)(
                    fmt::format("retrieving subtree at {} in git repository "
                                "{} failed with:\n{}",
                                subdir,
                                GetGitCAS()->git_path_.string(),
                                GitLastError()),
                    true /*fatal*/);
                // cleanup resources
                git_tree_entry_free(subtree_entry_ptr);
                return std::nullopt;
            }
            auto subtree_entry =
                std::unique_ptr<git_tree_entry, decltype(&tree_entry_closer)>(
                    subtree_entry_ptr, tree_entry_closer);

            std::string subtree_hash{
                git_oid_tostr_s(git_tree_entry_id(subtree_entry.get()))};
            return subtree_hash;
        }
        // if no subdir, get hash from tree
        std::string tree_hash{git_oid_tostr_s(git_tree_id(tree.get()))};
        return tree_hash;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get subtree from commit failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetSubtreeFromTree(std::string const& tree_id,
                                 std::string const& subdir,
                                 anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // check if subdir is not trivial
        if (subdir != ".") {
            // preferably with a "fake" repository!
            if (not IsRepoFake()) {
                Logger::Log(LogLevel::Debug,
                            "Subtree id retrieval from tree called on a real "
                            "repository");
            }
            // share the odb lock
            std::shared_lock lock{GetGitCAS()->mutex_};

            // get tree object from tree id
            git_oid tree_oid;
            if (git_oid_fromstr(&tree_oid, tree_id.c_str()) != 0) {
                (*logger)(
                    fmt::format("tree ID parsing in git repository {} failed "
                                "with:\n{}",
                                GetGitCAS()->git_path_.string(),
                                GitLastError()),
                    true /*fatal*/);
                return std::nullopt;
            }

            git_tree* tree_ptr{nullptr};
            if (git_tree_lookup(&tree_ptr, repo_->Ptr(), &tree_oid) != 0) {
                (*logger)(fmt::format(
                              "retrieving tree {} in git repository {} failed "
                              "with:\n{}",
                              tree_id,
                              GetGitCAS()->git_path_.string(),
                              GitLastError()),
                          true /*fatal*/);
                // cleanup resources
                git_tree_free(tree_ptr);
                return std::nullopt;
            }
            auto tree = std::unique_ptr<git_tree, decltype(&tree_closer)>(
                tree_ptr, tree_closer);

            // get hash for actual subdir
            git_tree_entry* subtree_entry_ptr{nullptr};
            if (git_tree_entry_bypath(
                    &subtree_entry_ptr, tree.get(), subdir.c_str()) != 0) {
                (*logger)(
                    fmt::format("retrieving subtree at {} in git repository "
                                "{} failed with:\n{}",
                                subdir,
                                GetGitCAS()->git_path_.string(),
                                GitLastError()),
                    true /*fatal*/);
                // cleanup resources
                git_tree_entry_free(subtree_entry_ptr);
                return std::nullopt;
            }
            auto subtree_entry =
                std::unique_ptr<git_tree_entry, decltype(&tree_entry_closer)>(
                    subtree_entry_ptr, tree_entry_closer);

            std::string subtree_hash{
                git_oid_tostr_s(git_tree_entry_id(subtree_entry.get()))};
            return subtree_hash;
        }
        // if no subdir, return given tree hash
        return tree_id;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get subtree from tree failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetSubtreeFromPath(std::filesystem::path const& fpath,
                                 std::string const& head_commit,
                                 anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            Logger::Log(
                LogLevel::Debug,
                "Subtree id retrieval from path called on a real repository");
        }
        // setup wrapped logger
        auto wrapped_logger = std::make_shared<anon_logger_t>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(
                    fmt::format("While getting repo root from path:\n{}", msg),
                    fatal);
            });
        // find root dir of this repository
        auto root = GetRepoRootFromPath(fpath, wrapped_logger);
        if (root == std::nullopt) {
            return std::nullopt;
        }

        // setup wrapped logger
        wrapped_logger = std::make_shared<anon_logger_t>(
            [logger](auto const& msg, bool fatal) {
                (*logger)(fmt::format("While going subtree hash retrieval from "
                                      "path:\n{}",
                                      msg),
                          fatal);
            });
        // find relative path from root to given path
        auto subdir = std::filesystem::relative(fpath, *root).string();
        // get subtree from head commit and subdir
        return GetSubtreeFromCommit(head_commit, subdir, wrapped_logger);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get subtree from path failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::CheckCommitExists(std::string const& commit,
                                anon_logger_ptr const& logger) noexcept
    -> std::optional<bool> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            Logger::Log(LogLevel::Debug,
                        "Commit lookup called on a real repository");
        }
        // lookup commit in current repo state
        git_oid commit_oid;
        if (git_oid_fromstr(&commit_oid, commit.c_str()) != 0) {
            (*logger)(
                fmt::format("commit ID parsing in git repository {} failed "
                            "with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            return std::nullopt;
        }

        git_commit* commit_obj = nullptr;
        int lookup_res{};
        {
            // share the odb lock
            std::shared_lock lock{GetGitCAS()->mutex_};
            lookup_res =
                git_commit_lookup(&commit_obj, repo_->Ptr(), &commit_oid);
        }
        if (lookup_res != 0) {
            if (lookup_res == GIT_ENOTFOUND) {
                // cleanup resources
                git_commit_free(commit_obj);
                return false;  // commit not found
            }
            // failure
            (*logger)(
                fmt::format("lookup of commit {} in git repository {} failed "
                            "with:\n{}",
                            commit,
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            // cleanup resources
            git_commit_free(commit_obj);
            return std::nullopt;
        }
        // cleanup resources
        git_commit_free(commit_obj);
        return true;  // commit exists
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "check commit exists failed with:\n{}", ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetRepoRootFromPath(std::filesystem::path const& fpath,
                                  anon_logger_ptr const& logger) noexcept
    -> std::optional<std::filesystem::path> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        GitContext::Create();  // initialize libgit2

        git_buf buffer = GIT_BUF_INIT_CONST(NULL, 0);
        auto res = git_repository_discover(&buffer, fpath.c_str(), 0, nullptr);

        if (res != 0) {
            if (res == GIT_ENOTFOUND) {
                git_buf_dispose(&buffer);
                return std::filesystem::path{};  // empty path cause nothing
                                                 // found
            }
            // failure
            (*logger)(fmt::format(
                          "repository root search failed at path {} with:\n{}!",
                          fpath.string(),
                          GitLastError()),
                      true /*fatal*/);
            git_buf_dispose(&buffer);
            return std::nullopt;
        }
        // found root repo path
        std::string result{buffer.ptr};
        git_buf_dispose(&buffer);
        // normalize root result
        auto actual_root =
            std::filesystem::path{result}.parent_path();  // remove trailing "/"
        if (actual_root.parent_path() / ".git" == actual_root) {
            return actual_root.parent_path();  // remove ".git" folder from path
        }
        return actual_root;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get repo root from path failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::CheckTreeExists(std::string const& tree_id,
                              anon_logger_ptr const& logger) noexcept
    -> std::optional<bool> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            Logger::Log(LogLevel::Debug,
                        "Subtree lookup called on a real repository");
        }
        // get git oid
        git_oid tree_oid;
        if (git_oid_fromstr(&tree_oid, tree_id.c_str()) != 0) {
            (*logger)(fmt::format("tree ID parsing in git repository {} failed "
                                  "with:\n{}",
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            return std::nullopt;
        }
        // get tree object
        git_tree* tree_ptr = nullptr;
        int lookup_res{};
        {
            // share the odb lock
            std::shared_lock lock{GetGitCAS()->mutex_};
            lookup_res = git_tree_lookup(&tree_ptr, repo_->Ptr(), &tree_oid);
        }
        git_tree_free(tree_ptr);
        if (lookup_res != 0) {
            if (lookup_res == GIT_ENOTFOUND) {
                return false;  // tree not found
            }
            (*logger)(
                fmt::format("tree lookup in git repository {} failed with:\n{}",
                            GetGitCAS()->git_path_.string(),
                            GitLastError()),
                true /*fatal*/);
            return std::nullopt;
        }
        return true;  // tree found
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "check subtree exists failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::IsRepoFake() const noexcept -> bool {
    return is_repo_fake_;
}

auto GitRepo::ReadTree(std::string const& id,
                       SymlinksCheckFunc const& check_symlinks,
                       bool is_hex_id,
                       bool ignore_special) const noexcept
    -> std::optional<tree_entries_t> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        // create object id
        auto oid = GitObjectID(id, is_hex_id);
        if (not oid) {
            return std::nullopt;
        }

        // lookup tree
        git_tree* tree_ptr{nullptr};
        {
            // share the odb lock
            std::shared_lock lock{GetGitCAS()->mutex_};
            if (git_tree_lookup(&tree_ptr, repo_->Ptr(), &(*oid)) != 0) {
                Logger::Log(LogLevel::Debug,
                            "failed to lookup Git tree {}",
                            is_hex_id ? std::string{id} : ToHexString(id));
                return std::nullopt;
            }
        }
        auto tree = std::unique_ptr<git_tree, decltype(&tree_closer)>{
            tree_ptr, tree_closer};

        // walk tree (flat) and create entries
        tree_entries_t entries{};
        entries.reserve(git_tree_entrycount(tree.get()));
        if (git_tree_walk(tree.get(),
                          GIT_TREEWALK_PRE,
                          ignore_special ? flat_tree_walker_ignore_special
                                         : flat_tree_walker,
                          &entries) != 0) {
            Logger::Log(LogLevel::Debug,
                        "failed to walk Git tree {}",
                        is_hex_id ? std::string{id} : ToHexString(id));
            return std::nullopt;
        }

        // checking non-upwardness of symlinks can not be easily or safely done
        // during the tree walk, so it is done here. This is only needed for
        // ignore_special==false.
        if (not ignore_special) {
            // we first gather all symlink candidates
            std::vector<bazel_re::Digest> symlinks{};
            symlinks.reserve(entries.size());  // at most one symlink per entry
            for (auto const& entry : entries) {
                for (auto const& item : entry.second) {
                    if (IsSymlinkObject(item.type)) {
                        symlinks.emplace_back(bazel_re::Digest(
                            ArtifactDigest(ToHexString(entry.first),
                                           /*size=*/0,
                                           /*is_tree=*/false)));
                        break;  // no need to check other items with same hash
                    }
                }
            }
            // we check symlinks in bulk, optimized for network-backed repos
            if (not check_symlinks) {
                Logger::Log(LogLevel::Debug, "check_symlink callable is empty");
                return std::nullopt;
            }
            if (not check_symlinks(symlinks)) {
                Logger::Log(LogLevel::Error,
                            "found upwards symlinks in Git tree {}",
                            is_hex_id ? std::string{id} : ToHexString(id));
                return std::nullopt;
            }
        }

#ifndef NDEBUG
        EnsuresAudit(ValidateEntries(entries));
#endif

        return entries;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "reading tree failed with:\n{}", ex.what());
    }
#endif

    return std::nullopt;
}

auto GitRepo::CreateTree(tree_entries_t const& entries) const noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
#ifndef NDEBUG
    ExpectsAudit(ValidateEntries(entries));
#endif  // NDEBUG
    // share the odb lock
    std::shared_lock lock{GetGitCAS()->mutex_};

    git_treebuilder* builder_ptr{nullptr};
    if (git_treebuilder_new(&builder_ptr, repo_->Ptr(), nullptr) != 0) {
        Logger::Log(LogLevel::Debug, "failed to create Git tree builder");
        return std::nullopt;
    }
    auto builder =
        std::unique_ptr<git_treebuilder, decltype(&treebuilder_closer)>{
            builder_ptr, treebuilder_closer};

    for (auto const& [raw_id, es] : entries) {
        auto id = GitObjectID(raw_id, /*is_hex_id=*/false);
        for (auto const& entry : es) {
            if (not id or git_treebuilder_insert(
                              nullptr,
                              builder.get(),
                              entry.name.c_str(),
                              &(*id),
                              ObjectTypeToGitFileMode(entry.type)) != 0) {
                Logger::Log(LogLevel::Debug,
                            "failed adding object {} to Git tree",
                            ToHexString(raw_id));
                return std::nullopt;
            }
        }
    }

    git_oid oid;
    if (git_treebuilder_write(&oid, builder.get()) != 0) {
        return std::nullopt;
    }
    auto raw_id = ToRawString(oid);
    if (not raw_id) {
        return std::nullopt;
    }
    return std::move(*raw_id);
#endif
}

auto GitRepo::ReadTreeData(std::string const& data,
                           std::string const& id,
                           SymlinksCheckFunc const& check_symlinks,
                           bool is_hex_id) noexcept
    -> std::optional<tree_entries_t> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        InMemoryODBBackend b{.parent = kInMemoryODBParent};
        auto cas = std::make_shared<GitCAS>();
        if (auto raw_id =
                is_hex_id ? FromHexString(id) : std::make_optional(id)) {
            try {
                b.trees.emplace(*raw_id, data);
            } catch (...) {
                return std::nullopt;
            }
            // create a GitCAS from a special-purpose in-memory object database.
            git_odb* odb_ptr{nullptr};
            if (git_odb_new(&odb_ptr) == 0 and
                git_odb_add_backend(
                    odb_ptr,
                    reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                    0) == 0) {
                cas->odb_.reset(odb_ptr);  // take ownership of odb
                // wrap odb in "fake" repo
                auto repo =
                    GitRepo(std::static_pointer_cast<GitCAS const>(cas));
                return repo.ReadTree(
                    *raw_id, check_symlinks, /*is_hex_id=*/false);
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "reading tree data failed with:\n{}", ex.what());
    }
#endif
    return std::nullopt;
}

auto GitRepo::CreateShallowTree(tree_entries_t const& entries) noexcept
    -> std::optional<std::pair<std::string, std::string>> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        InMemoryODBBackend b{.parent = kInMemoryODBParent, .entries = &entries};
        auto cas = std::make_shared<GitCAS>();
        // create a GitCAS from a special-purpose in-memory object database.
        git_odb* odb_ptr{nullptr};
        if (git_odb_new(&odb_ptr) == 0 and
            git_odb_add_backend(
                odb_ptr,
                reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                0) == 0) {
            cas->odb_.reset(odb_ptr);  // take ownership of odb
            // wrap odb in "fake" repo
            auto repo = GitRepo(std::static_pointer_cast<GitCAS const>(cas));
            if (auto raw_id = repo.CreateTree(entries)) {
                // read result from in-memory trees
                if (auto it = b.trees.find(*raw_id); it != b.trees.end()) {
                    return std::make_pair(std::move(*raw_id),
                                          std::move(it->second));
                }
            }
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "creating shallow tree failed with:\n{}",
                    ex.what());
    }
#endif
    return std::nullopt;
}

void GitRepo::PopulateStrarray(
    git_strarray* array,
    std::vector<std::string> const& string_list) noexcept {
    array->count = string_list.size();
    array->strings = gsl::owner<char**>(new char*[string_list.size()]);
    for (auto const& elem : string_list) {
        auto i = static_cast<size_t>(&elem - &string_list[0]);  // get index
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        array->strings[i] = gsl::owner<char*>(new char[elem.size() + 1]);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        strncpy(array->strings[i], elem.c_str(), elem.size() + 1);
    }
}
