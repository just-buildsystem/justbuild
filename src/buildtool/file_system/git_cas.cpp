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

#include "src/buildtool/file_system/git_cas.hpp"

#include <mutex>
#include <sstream>

#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL

extern "C" {
#include <git2.h>
#include <git2/sys/odb_backend.h>
}

namespace {

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

[[maybe_unused]] [[nodiscard]] auto ValidateEntries(
    GitCAS::tree_entries_t const& entries) -> bool {
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

auto const repo_closer = [](gsl::owner<git_repository*> repo) {
    if (repo != nullptr) {
        git_repository_free(repo);
    }
};

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
        reinterpret_cast<GitCAS::tree_entries_t*>(payload);  // NOLINT

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
    GitCAS::tree_entries_t const* entries{nullptr};        // object headers
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

auto GitCAS::Open(std::filesystem::path const& repo_path) noexcept
    -> GitCASPtr {
#ifndef BOOTSTRAP_BUILD_TOOL
    try {
        auto cas = std::make_shared<GitCAS>();
        if (cas->OpenODB(repo_path)) {
            return std::static_pointer_cast<GitCAS const>(cas);
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error,
                    "opening git object database failed with:\n{}",
                    ex.what());
    }
#endif
    return nullptr;
}

GitCAS::~GitCAS() noexcept {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (odb_ != nullptr) {
        git_odb_free(odb_);
        odb_ = nullptr;
    }
#endif
}

auto GitCAS::ReadObject(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    if (odb_ == nullptr) {
        return std::nullopt;
    }

    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    git_odb_object* obj = nullptr;
    if (git_odb_read(&obj, odb_, &oid.value()) != 0) {
        Logger::Log(LogLevel::Error,
                    "reading git object {} from database failed with:\n{}",
                    is_hex_id ? id : ToHexString(id),
                    GitLastError());
        return std::nullopt;
    }

    std::string data(static_cast<char const*>(git_odb_object_data(obj)),
                     git_odb_object_size(obj));
    git_odb_object_free(obj);

    return data;
#endif
}

auto GitCAS::ReadTree(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<tree_entries_t> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    // create object id
    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    // create fake repository from ODB
    git_repository* repo_ptr{nullptr};
    if (git_repository_wrap_odb(&repo_ptr, odb_) != 0) {
        Logger::Log(LogLevel::Debug,
                    "failed to create fake Git repository from object db");
        return std::nullopt;
    }
    auto fake_repo = std::unique_ptr<git_repository, decltype(repo_closer)>{
        repo_ptr, repo_closer};

    // lookup tree
    git_tree* tree_ptr{nullptr};
    if (git_tree_lookup(&tree_ptr, fake_repo.get(), &(*oid)) != 0) {
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

    gsl_EnsuresAudit(ValidateEntries(entries));

    return entries;
#endif
}

auto GitCAS::ReadHeader(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::pair<std::size_t, ObjectType>> {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (odb_ == nullptr) {
        return std::nullopt;
    }

    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    std::size_t size{};
    git_object_t type{};
    if (git_odb_read_header(&size, &type, odb_, &oid.value()) != 0) {
        Logger::Log(LogLevel::Error,
                    "reading git object header {} from database failed "
                    "with:\n{}",
                    is_hex_id ? id : ToHexString(id),
                    GitLastError());
        return std::nullopt;
    }

    if (auto obj_type = GitTypeToObjectType(type)) {
        return std::make_pair(size, *obj_type);
    }
#endif
    return std::nullopt;
}

auto GitCAS::CreateTree(GitCAS::tree_entries_t const& entries) const noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    gsl_ExpectsAudit(ValidateEntries(entries));

    // create fake repository from ODB
    git_repository* repo_ptr{nullptr};
    if (git_repository_wrap_odb(&repo_ptr, odb_) != 0) {
        Logger::Log(LogLevel::Debug,
                    "failed to create fake Git repository from object db");
        return std::nullopt;
    }
    auto fake_repo = std::unique_ptr<git_repository, decltype(repo_closer)>{
        repo_ptr, repo_closer};

    git_treebuilder* builder_ptr{nullptr};
    if (git_treebuilder_new(&builder_ptr, fake_repo.get(), nullptr) != 0) {
        Logger::Log(LogLevel::Debug, "failed to create Git tree builder");
        return std::nullopt;
    }
    auto builder =
        std::unique_ptr<git_treebuilder, decltype(treebuilder_closer)>{
            builder_ptr, treebuilder_closer};

    for (auto const& [raw_id, es] : entries) {
        auto id = GitObjectID(raw_id, /*is_hex_id=*/false);
        for (auto const& entry : es) {
            git_tree_entry const* tree_entry{nullptr};
            if (not id or git_treebuilder_insert(
                              &tree_entry,
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

auto GitCAS::OpenODB(std::filesystem::path const& repo_path) noexcept -> bool {
    static std::mutex repo_mutex{};
#ifdef BOOTSTRAP_BUILD_TOOL
    return false;
#else
    {  // lock as git_repository API has no thread-safety guarantees
        std::unique_lock lock{repo_mutex};
        git_repository* repo = nullptr;
        if (git_repository_open(&repo, repo_path.c_str()) != 0) {
            Logger::Log(LogLevel::Error,
                        "opening git repository {} failed with:\n{}",
                        repo_path.string(),
                        GitLastError());
            return false;
        }
        git_repository_odb(&odb_, repo);
        git_repository_free(repo);
    }
    if (odb_ == nullptr) {
        Logger::Log(LogLevel::Error,
                    "obtaining git object database {} failed with:\n{}",
                    repo_path.string(),
                    GitLastError());
        return false;
    }
    return true;
#endif
}

auto GitCAS::ReadTreeData(std::string const& data,
                          std::string const& id,
                          bool is_hex_id) noexcept
    -> std::optional<tree_entries_t> {
#ifndef BOOTSTRAP_BUILD_TOOL
    InMemoryODBBackend b{kInMemoryODBParent};
    GitCAS cas{};
    if (auto raw_id = is_hex_id ? FromHexString(id) : std::make_optional(id)) {
        try {
            b.trees.emplace(*raw_id, data);
        } catch (...) {
            return std::nullopt;
        }
        // create a GitCAS from a special-purpose in-memory object database.
        if (git_odb_new(&cas.odb_) == 0 and
            git_odb_add_backend(
                cas.odb_,
                reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                0) == 0) {
            return cas.ReadTree(*raw_id, /*is_hex_id=*/false);
        }
    }
#endif
    return std::nullopt;
}

auto GitCAS::CreateShallowTree(GitCAS::tree_entries_t const& entries) noexcept
    -> std::optional<std::pair<std::string, std::string>> {
#ifndef BOOTSTRAP_BUILD_TOOL
    InMemoryODBBackend b{kInMemoryODBParent, &entries};
    GitCAS cas{};
    // create a GitCAS from a special-purpose in-memory object database.
    if (git_odb_new(&cas.odb_) == 0 and
        git_odb_add_backend(cas.odb_,
                            reinterpret_cast<git_odb_backend*>(&b),  // NOLINT
                            0) == 0) {
        if (auto raw_id = cas.CreateTree(entries)) {
            // read result from in-memory trees
            if (auto it = b.trees.find(*raw_id); it != b.trees.end()) {
                return std::make_pair(std::move(*raw_id),
                                      std::move(it->second));
            }
        }
    }
#endif
    return std::nullopt;
}
