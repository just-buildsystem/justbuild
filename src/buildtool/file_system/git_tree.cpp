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

#include "src/buildtool/file_system/git_tree.hpp"

#include <sstream>

#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

extern "C" {
#include <git2.h>
}

namespace {

// resolve '.' and '..' in path.
[[nodiscard]] auto ResolveRelativePath(
    std::filesystem::path const& path) noexcept -> std::filesystem::path {
    auto normalized = path.lexically_normal();
    return (normalized / "").parent_path();  // strip trailing slash
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto LookupEntryPyPath(
    GitTree const& tree,
    std::filesystem::path::const_iterator it,
    std::filesystem::path::const_iterator const& end) noexcept
    -> GitTreeEntryPtr {
    auto segment = *it;
    auto entry = tree.LookupEntryByName(segment);
    if (not entry) {
        return nullptr;
    }
    if (++it != end) {
        if (not entry->IsTree()) {
            return nullptr;
        }
        return LookupEntryPyPath(*entry->Tree(), it, end);
    }
    return entry;
}

}  // namespace

auto GitTree::Read(std::filesystem::path const& repo_path,
                   std::string const& tree_id) noexcept
    -> std::optional<GitTree> {
    auto cas = GitCAS::Open(repo_path);
    if (not cas) {
        return std::nullopt;
    }
    return Read(cas, tree_id);
}

auto GitTree::Read(gsl::not_null<GitCASPtr> const& cas,
                   std::string const& tree_id) noexcept
    -> std::optional<GitTree> {
    if (auto raw_id = FromHexString(tree_id)) {
        auto repo = GitRepo::Open(cas);
        if (repo != std::nullopt) {
            if (auto entries = repo->ReadTree(*raw_id)) {
                return GitTree::FromEntries(cas, std::move(*entries), *raw_id);
            }
        }
        else {
            return ::std::nullopt;
        }
    }
    return std::nullopt;
}

auto GitTree::LookupEntryByName(std::string const& name) const noexcept
    -> GitTreeEntryPtr {
    auto entry_it = entries_.find(name);
    if (entry_it == entries_.end()) {
        Logger::Log(
            LogLevel::Error, "git tree does not contain entry {}", name);
        return nullptr;
    }
    return entry_it->second;
}

auto GitTree::LookupEntryByPath(
    std::filesystem::path const& path) const noexcept -> GitTreeEntryPtr {
    auto resolved = ResolveRelativePath(path);
    return LookupEntryPyPath(*this, resolved.begin(), resolved.end());
}

auto GitTree::Size() const noexcept -> std::optional<std::size_t> {
    if (auto header = cas_->ReadHeader(raw_id_)) {
        return header->first;
    }
    return std::nullopt;
}

auto GitTree::RawData() const noexcept -> std::optional<std::string> {
    return cas_->ReadObject(raw_id_);
}

auto GitTreeEntry::Blob() const noexcept -> std::optional<std::string> {
    if (not IsBlob()) {
        return std::nullopt;
    }
    return cas_->ReadObject(raw_id_);
}

auto GitTreeEntry::Tree() const& noexcept -> std::optional<GitTree> const& {
    return tree_cached_.SetOnceAndGet([this]() -> std::optional<GitTree> {
        if (IsTree()) {
            auto repo = GitRepo::Open(cas_);
            if (repo == std::nullopt) {
                return std::nullopt;
            }
            if (auto entries = repo->ReadTree(raw_id_)) {
                return GitTree::FromEntries(cas_, std::move(*entries), raw_id_);
            }
        }
        return std::nullopt;
    });
}

auto GitTreeEntry::Size() const noexcept -> std::optional<std::size_t> {
    if (auto header = cas_->ReadHeader(raw_id_)) {
        return header->first;
    }
    return std::nullopt;
}

auto GitTreeEntry::RawData() const noexcept -> std::optional<std::string> {
    return cas_->ReadObject(raw_id_);
}
