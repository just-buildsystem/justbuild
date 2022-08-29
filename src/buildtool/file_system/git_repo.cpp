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

auto const tree_closer = [](gsl::owner<git_tree*> tree) {
    if (tree != nullptr) {
        git_tree_free(tree);
    }
};

auto const treebuilder_closer = [](gsl::owner<git_treebuilder*> builder) {
    if (builder != nullptr) {
        git_treebuilder_free(builder);
    }
};

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
    if (git_tree_lookup(&tree_ptr, repo_, &(*oid)) != 0) {
        Logger::Log(LogLevel::Debug,
                    "failed to lookup Git tree {}",
                    is_hex_id ? std::string{id} : ToHexString(id));
        return std::nullopt;
    }
    auto tree =
        std::unique_ptr<git_tree, decltype(tree_closer)>{tree_ptr, tree_closer};

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
    if (git_treebuilder_new(&builder_ptr, repo_, nullptr) != 0) {
        Logger::Log(LogLevel::Debug, "failed to create Git tree builder");
        return std::nullopt;
    }
    auto builder =
        std::unique_ptr<git_treebuilder, decltype(treebuilder_closer)>{
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
            if (git_odb_new(&cas->odb_) == 0 and
                git_odb_add_backend(
                    cas->odb_,
                    reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                    0) == 0) {
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
    -> std::optional<std::pair<std::string, std::string>> {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        InMemoryODBBackend b{kInMemoryODBParent, &entries};
        auto cas = std::make_shared<GitCAS>();
        // create a GitCAS from a special-purpose in-memory object database.
        if (git_odb_new(&cas->odb_) == 0 and
            git_odb_add_backend(
                cas->odb_,
                reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                0) == 0) {
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
