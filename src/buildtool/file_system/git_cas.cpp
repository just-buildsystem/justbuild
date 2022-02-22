#include "src/buildtool/file_system/git_cas.hpp"

#include <sstream>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

extern "C" {
#include <git2.h>
}

namespace {

constexpr auto kOIDRawSize{GIT_OID_RAWSZ};
constexpr auto kOIDHexSize{GIT_OID_HEXSZ};

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
    if ((is_hex_id and id.size() < kOIDHexSize) or id.size() < kOIDRawSize) {
        Logger::Log(LogLevel::Error,
                    "invalid git object id {}",
                    is_hex_id ? id : ToHexString(id));
        return std::nullopt;
    }
    git_oid oid{};
    if (is_hex_id and git_oid_fromstr(&oid, id.data()) == 0) {
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

}  // namespace

auto GitCAS::Open(std::filesystem::path const& repo_path) noexcept
    -> GitCASPtr {
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
    return nullptr;
}

GitCAS::GitCAS() noexcept {
    if (not(initialized_ = (git_libgit2_init() >= 0))) {
        Logger::Log(LogLevel::Error, "initializing libgit2 failed");
    }
}
GitCAS::~GitCAS() noexcept {
    if (odb_ != nullptr) {
        git_odb_free(odb_);
        odb_ = nullptr;
    }
    if (initialized_) {
        git_libgit2_shutdown();
    }
}

auto GitCAS::ReadObject(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::string> {
    if (not initialized_) {
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
}

auto GitCAS::ReadHeader(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::pair<std::size_t, ObjectType>> {
    if (not initialized_) {
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

    return std::nullopt;
}

auto GitCAS::OpenODB(std::filesystem::path const& repo_path) noexcept -> bool {
    if (initialized_) {
        {  // lock as git_repository API has no thread-safety guarantees
            std::unique_lock lock{repo_mutex_};
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
    }
    return initialized_;
}
