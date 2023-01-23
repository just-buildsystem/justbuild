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
#include "src/utils/cpp/hex_string.hpp"
#include "src/utils/cpp/path.hpp"

extern "C" {
#include <git2.h>
#include <git2/sys/odb_backend.h>
}

namespace {

constexpr std::size_t kWaitTime{2};  // time in ms between tries for git locks

constexpr std::size_t kOIDRawSize{GIT_OID_RAWSZ};
constexpr std::size_t kOIDHexSize{GIT_OID_HEXSZ};

[[nodiscard]] auto GitLastError() noexcept -> std::string {
    git_error const* err{nullptr};
    if ((err = git_error_last()) != nullptr and err->message != nullptr) {
        return fmt::format("error code {}: {}", err->klass, err->message);
    }
    return "<unknown error>";
}

[[nodiscard]] auto GitObjectID(std::string const& id,
                               bool is_hex_id = false) noexcept
    -> std::optional<git_oid> {
    if (id.size() < (is_hex_id ? kOIDHexSize : kOIDRawSize)) {
        Logger::Log(LogLevel::Error,
                    "invalid git object id {}",
                    is_hex_id ? id : ToHexString(id));
        return std::nullopt;
    }
    git_oid oid{};
    if (is_hex_id and git_oid_fromstr(&oid, id.c_str()) == 0) {
        return oid;
    }
    if (not is_hex_id and
        git_oid_fromraw(
            &oid,
            reinterpret_cast<unsigned char const*>(id.data())  // NOLINT
            ) == 0) {
        return oid;
    }
    Logger::Log(LogLevel::Error,
                "parsing git object id {} failed with:\n{}",
                is_hex_id ? id : ToHexString(id),
                GitLastError());
    return std::nullopt;
}

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

[[nodiscard]] auto GitFileModeToObjectType(git_filemode_t const& mode) noexcept
    -> std::optional<ObjectType> {
    switch (mode) {
        case GIT_FILEMODE_BLOB:
            return ObjectType::File;
        case GIT_FILEMODE_BLOB_EXECUTABLE:
            return ObjectType::Executable;
        case GIT_FILEMODE_TREE:
            return ObjectType::Tree;
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
            (*entries)[*raw_id].emplace_back(std::move(name), *type);
            return 1;  // return >=0 on success, 1 == skip subtrees (flat)
        }
    }
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

#ifndef BOOTSTRAP_BUILD_TOOL

// A backend that can be used to read and create tree objects in-memory.
auto const kInMemoryODBParent = CreateInMemoryODBParent();

#endif  // BOOTSTRAP_BUILD_TOOL

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

#ifndef BOOTSTRAP_BUILD_TOOL

// A backend that can be used to fetch from the remote of another repository.
auto const kFetchIntoODBParent = CreateFetchIntoODBParent();

#endif  // BOOTSTRAP_BUILD_TOOL

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
    if (git_cas != nullptr) {
        git_repository* repo_ptr{nullptr};
        if (git_repository_wrap_odb(&repo_ptr, git_cas->odb_.get()) != 0) {
            Logger::Log(LogLevel::Error,
                        "could not create wrapper for git repository");
            git_repository_free(repo_ptr);
            return;
        }
        repo_.reset(repo_ptr);  // retain repo
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
        git_repository* repo_ptr{nullptr};
        if (git_repository_open(&repo_ptr, repo_path.c_str()) != 0) {
            Logger::Log(LogLevel::Error,
                        "opening git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            git_repository_free(repo_ptr);
            repo_ = nullptr;
            return;
        }
        repo_.reset(repo_ptr);  // retain repo pointer
        // get odb
        git_odb* odb_ptr{nullptr};
        git_repository_odb(&odb_ptr, repo_.get());
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
        // save root path
        cas->git_path_ = ToNormalPath(std::filesystem::absolute(
            std::filesystem::path(git_repository_path(repo_.get()))));
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
    git_cas_ = std::move(other.git_cas_);
    repo_ = std::move(other.repo_);
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
        // add all files to be staged
        git_index* index_ptr{nullptr};
        git_repository_index(&index_ptr, repo_.get());
        auto index = std::unique_ptr<git_index, decltype(&index_closer)>(
            index_ptr, index_closer);

        git_strarray array_obj{};
        PopulateStrarray(&array_obj, {"."});
        auto array = std::unique_ptr<git_strarray, decltype(&strarray_deleter)>(
            &array_obj, strarray_deleter);

        if (git_index_add_all(index.get(), array.get(), 0, nullptr, nullptr) !=
            0) {
            (*logger)(fmt::format(
                          "staging files in git repository {} failed with:\n{}",
                          GetGitCAS()->git_path_.string(),
                          GitLastError()),
                      true /*fatal*/);
            return std::nullopt;
        }
        // release unused resources
        array.reset(nullptr);
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
        if (git_tree_lookup(&tree_ptr, repo_.get(), &tree_oid) != 0) {
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
                                repo_.get(),
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
        // get commit spec
        git_object* target_ptr{nullptr};
        if (git_revparse_single(&target_ptr, repo_.get(), commit.c_str()) !=
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

        size_t max_attempts = 3;  // number of tries
        int err = 0;
        git_strarray tag_names{};
        while (max_attempts > 0) {
            --max_attempts;
            err = git_tag_create(&oid,
                                 repo_.get(),
                                 name.c_str(),
                                 target.get(),
                                 tagger.get(),
                                 message.c_str(),
                                 1 /*force*/);
            if (err == 0) {
                return true;  // success!
            }
            // check if tag hasn't already been added by another process
            if (git_tag_list_match(&tag_names, name.c_str(), repo_.get()) ==
                    0 and
                tag_names.count > 0) {
                git_strarray_dispose(&tag_names);
                return true;  // success!
            }
            // tag still not in, so sleep and try again
            std::this_thread::sleep_for(std::chrono::milliseconds(kWaitTime));
        }
        (*logger)(fmt::format("tag creation in git repository {} failed",
                              GetGitCAS()->git_path_.string()),
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
        // get root commit id
        git_oid head_oid;
        if (git_reference_name_to_id(&head_oid, repo_.get(), "HEAD") != 0) {
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

auto GitRepo::GetBranchLocalRefname(std::string const& branch,
                                    anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // only possible for real repository!
        if (IsRepoFake()) {
            (*logger)("cannot retrieve branch refname using a fake repository!",
                      true /*fatal*/);
            return std::nullopt;
        }
        // get local reference of branch
        git_reference* local_ref = nullptr;
        if (git_branch_lookup(
                &local_ref, repo_.get(), branch.c_str(), GIT_BRANCH_LOCAL) !=
            0) {
            (*logger)(fmt::format("retrieving branch {} local reference in git "
                                  "repository {} failed with:\n{}",
                                  branch,
                                  GetGitCAS()->git_path_.string(),
                                  GitLastError()),
                      true /*fatal*/);
            // release resources
            git_reference_free(local_ref);
            return std::nullopt;
        }
        auto refname = std::string(git_reference_name(local_ref));
        // release resources
        git_reference_free(local_ref);
        return refname;
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "get branch local refname failed with:\n{}",
                    ex.what());
        return std::nullopt;
    }
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetCommitFromRemote(std::string const& repo_url,
                                  std::string const& branch,
                                  anon_logger_ptr const& logger) noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
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
                &remote_ptr, repo_.get(), repo_url.c_str()) != 0) {
            (*logger)(
                fmt::format("creating anonymous remote for git repository {} "
                            "failed with:\n{}",
                            GetGitCAS()->git_path_.string(),
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
                            GetGitCAS()->git_path_.string(),
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
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::FetchFromRemote(std::string const& repo_url,
                              std::optional<std::string> const& branch,
                              anon_logger_ptr const& logger) noexcept -> bool {
#ifdef BOOTSTRAP_BUILD_TOOL
    return false;
#else
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
                &remote_ptr, repo_.get(), repo_url.c_str()) != 0) {
            (*logger)(fmt::format("creating remote {} for git repository {} "
                                  "failed with:\n{}",
                                  repo_url,
                                  GetGitCAS()->git_path_.string(),
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
            SetCustomSSLCertificateCheckCallback(repo_.get());

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
                            GetGitCAS()->git_path_.string(),
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
            (*logger)(
                "WARNING: subtree id retireval from commit called on a real "
                "repository!\n",
                false /*fatal*/);
        }
        // get commit object
        git_oid commit_oid;
        git_oid_fromstr(&commit_oid, commit.c_str());

        git_commit* commit_ptr{nullptr};
        if (git_commit_lookup(&commit_ptr, repo_.get(), &commit_oid) != 0) {
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
                (*logger)(
                    "WARNING: subtree id retrieval from tree called on a real "
                    "repository!\n",
                    false /*fatal*/);
            }
            // get tree object from tree id
            git_oid tree_oid;
            git_oid_fromstr(&tree_oid, tree_id.c_str());

            git_tree* tree_ptr{nullptr};
            if (git_tree_lookup(&tree_ptr, repo_.get(), &tree_oid) != 0) {
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
            (*logger)(
                "WARNING: subtree id retrieval from path called on a real "
                "repository!\n",
                false /*fatal*/);
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
            (*logger)("WARNING: commit lookup called on a real repository!\n",
                      false /*fatal*/);
        }
        // lookup commit in current repo state
        git_oid commit_oid;
        git_oid_fromstr(&commit_oid, commit.c_str());
        git_commit* commit_obj = nullptr;
        auto lookup_res =
            git_commit_lookup(&commit_obj, repo_.get(), &commit_oid);
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

auto GitRepo::UpdateCommitViaTmpRepo(std::filesystem::path const& tmp_repo_path,
                                     std::string const& repo_url,
                                     std::string const& branch,
                                     anon_logger_ptr const& logger)
    const noexcept -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            (*logger)("WARNING: commit update called on a real repository!\n",
                      false /*fatal*/);
        }
        // create the temporary real repository
        auto tmp_repo = GitRepo::InitAndOpen(tmp_repo_path, /*is_bare=*/true);
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
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::FetchViaTmpRepo(std::filesystem::path const& tmp_repo_path,
                              std::string const& repo_url,
                              std::optional<std::string> const& branch,
                              anon_logger_ptr const& logger) noexcept -> bool {
#ifdef BOOTSTRAP_BUILD_TOOL
    return false;
#else
    try {
        // preferably with a "fake" repository!
        if (not IsRepoFake()) {
            (*logger)("WARNING: branch fetch called on a real repository!\n",
                      false /*fatal*/);
        }
        // create the temporary real repository
        // it can be bare, as the refspecs for this fetch will be given
        // explicitly.
        auto tmp_repo = GitRepo::InitAndOpen(tmp_repo_path, /*is_bare=*/true);
        if (tmp_repo == std::nullopt) {
            return false;
        }
        // add backend, with max priority
        FetchIntoODBBackend b{kFetchIntoODBParent, git_cas_->odb_.get()};
        if (git_odb_add_backend(
                tmp_repo->GetGitCAS()->odb_.get(),
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
#endif  // BOOTSTRAP_BUILD_TOOL
}

auto GitRepo::GetRepoRootFromPath(std::filesystem::path const& fpath,
                                  anon_logger_ptr const& logger) noexcept
    -> std::optional<std::filesystem::path> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    try {
        auto git_state = GitContext();  // initialize libgit2

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

auto GitRepo::IsRepoFake() const noexcept -> bool {
    return is_repo_fake_;
}

auto GitRepo::ReadTree(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<tree_entries_t> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    // create object id
    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    // lookup tree
    git_tree* tree_ptr{nullptr};
    if (git_tree_lookup(&tree_ptr, repo_.get(), &(*oid)) != 0) {
        Logger::Log(LogLevel::Debug,
                    "failed to lookup Git tree {}",
                    is_hex_id ? std::string{id} : ToHexString(id));
        return std::nullopt;
    }
    auto tree = std::unique_ptr<git_tree, decltype(&tree_closer)>{tree_ptr,
                                                                  tree_closer};

    // walk tree (flat) and create entries
    tree_entries_t entries{};
    entries.reserve(git_tree_entrycount(tree.get()));
    if (git_tree_walk(
            tree.get(), GIT_TREEWALK_PRE, flat_tree_walker, &entries) != 0) {
        Logger::Log(LogLevel::Debug,
                    "failed to walk Git tree {}",
                    is_hex_id ? std::string{id} : ToHexString(id));
        return std::nullopt;
    }

#ifndef NDEBUG
    gsl_EnsuresAudit(ValidateEntries(entries));
#endif

    return entries;
#endif
}

auto GitRepo::CreateTree(tree_entries_t const& entries) const noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
#ifndef NDEBUG
    gsl_ExpectsAudit(ValidateEntries(entries));
#endif  // NDEBUG

    git_treebuilder* builder_ptr{nullptr};
    if (git_treebuilder_new(&builder_ptr, repo_.get(), nullptr) != 0) {
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
                           bool is_hex_id) noexcept
    -> std::optional<tree_entries_t> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        InMemoryODBBackend b{kInMemoryODBParent};
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
                return repo.ReadTree(*raw_id, /*is_hex_id=*/false);
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
    -> std::optional<std::pair<std::string, std::string> > {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        InMemoryODBBackend b{kInMemoryODBParent, &entries};
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
