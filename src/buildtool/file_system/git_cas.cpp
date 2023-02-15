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

#include <cstring>
#include <mutex>
#include <sstream>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL

extern "C" {
#include <git2.h>
}

namespace {

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

auto GitCAS::ReadObject(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::string> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
    if (not odb_) {
        return std::nullopt;
    }

    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    git_odb_object* obj = nullptr;
    if (git_odb_read(&obj, odb_.get(), &oid.value()) != 0) {
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

auto GitCAS::ReadHeader(std::string const& id, bool is_hex_id) const noexcept
    -> std::optional<std::pair<std::size_t, ObjectType>> {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (not odb_) {
        return std::nullopt;
    }

    auto oid = GitObjectID(id, is_hex_id);
    if (not oid) {
        return std::nullopt;
    }

    std::size_t size{};
    git_object_t type{};
    if (git_odb_read_header(&size, &type, odb_.get(), &oid.value()) != 0) {
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
        git_odb* odb_ptr{nullptr};
        git_repository_odb(&odb_ptr, repo);
        odb_.reset(odb_ptr);  // retain odb pointer
        // set root
        git_path_ = std::filesystem::weakly_canonical(std::filesystem::absolute(
            std::filesystem::path(git_repository_path(repo))));
        // release resources
        git_repository_free(repo);
    }
    if (not odb_) {
        Logger::Log(LogLevel::Error,
                    "obtaining git object database {} failed with:\n{}",
                    repo_path.string(),
                    GitLastError());
        return false;
    }
    return true;
#endif
}
