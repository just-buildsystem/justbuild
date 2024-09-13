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

#include "src/buildtool/file_system/git_utils.hpp"

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

extern "C" {
#include <git2.h>
}

#ifndef BOOTSTRAP_BUILD_TOOL
namespace {
constexpr std::size_t kOIDRawSize{GIT_OID_RAWSZ};
constexpr std::size_t kOIDHexSize{GIT_OID_HEXSZ};
}  // namespace
#endif  // BOOTSTRAP_BUILD_TOOL

auto GitLastError() noexcept -> std::string {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_error const* const err = git_error_last();
    if (err != nullptr and err->message != nullptr) {
        return fmt::format("error code {}: {}", err->klass, err->message);
    }
#endif  // BOOTSTRAP_BUILD_TOOL
    return "<unknown error>";
}

auto GitObjectID(std::string const& id,
                 bool is_hex_id) noexcept -> std::optional<git_oid> {
#ifdef BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
#else
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
#endif  // BOOTSTRAP_BUILD_TOOL
}

void odb_closer(gsl::owner<git_odb*> odb) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_odb_free(odb);
#endif
}

void tree_closer(gsl::owner<git_tree*> tree) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_tree_free(tree);
#endif
}

void treebuilder_closer(gsl::owner<git_treebuilder*> builder) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_treebuilder_free(builder);
#endif
}

void index_closer(gsl::owner<git_index*> index) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_index_free(index);
#endif
}

void strarray_closer(gsl::owner<git_strarray*> strarray) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_strarray_dispose(strarray);
#endif
}

void strarray_deleter(gsl::owner<git_strarray*> strarray) {
#ifndef BOOTSTRAP_BUILD_TOOL
    if (strarray->strings != nullptr) {
        for (std::size_t i = 0; i < strarray->count; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-pro-bounds-pointer-arithmetic)
            delete[] strarray->strings[i];
        }
        delete[] strarray->strings;
        strarray->strings = nullptr;
        strarray->count = 0;
    }
#endif
}

void signature_closer(gsl::owner<git_signature*> signature) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_signature_free(signature);
#endif
}

void object_closer(gsl::owner<git_object*> object) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_object_free(object);
#endif
}

void remote_closer(gsl::owner<git_remote*> remote) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_remote_free(remote);
#endif
}

void commit_closer(gsl::owner<git_commit*> commit) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_commit_free(commit);
#endif
}

void tree_entry_closer(gsl::owner<git_tree_entry*> tree_entry) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_tree_entry_free(tree_entry);
#endif
}

void config_closer(gsl::owner<git_config*> cfg) {
#ifndef BOOTSTRAP_BUILD_TOOL
    git_config_free(cfg);
#endif
}
