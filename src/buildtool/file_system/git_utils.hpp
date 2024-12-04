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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP

#include <cstddef>
#include <optional>
#include <string>

#include "gsl/gsl"

extern "C" {
struct git_oid;
struct git_odb;
struct git_repository;
struct git_tree;
struct git_signature;
struct git_object;
struct git_remote;
struct git_commit;
struct git_tree_entry;
struct git_config;
}

// time in ms between tries for git locks
constexpr std::size_t kGitLockWaitTime{100};
// number of retries for git locks
constexpr std::size_t kGitLockNumTries{10};

[[nodiscard]] auto GitObjectID(std::string const& id,
                               bool is_hex_id = false) noexcept
    -> std::optional<git_oid>;

/// \brief Retrieve error message of last libgit2 call.
[[nodiscard]] auto GitLastError() noexcept -> std::string;

void odb_closer(gsl::owner<git_odb*> odb);

void repository_closer(gsl::owner<git_repository*> repository);

void tree_closer(gsl::owner<git_tree*> tree);

void signature_closer(gsl::owner<git_signature*> signature);

void object_closer(gsl::owner<git_object*> object);

void remote_closer(gsl::owner<git_remote*> remote);

void commit_closer(gsl::owner<git_commit*> commit);

void tree_entry_closer(gsl::owner<git_tree_entry*> tree_entry);

void config_closer(gsl::owner<git_config*> cfg);

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_GIT_UTILS_HPP
