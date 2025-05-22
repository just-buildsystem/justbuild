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

#include <algorithm>
#include <functional>
#include <vector>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace {

// resolve '.' and '..' in path.
[[nodiscard]] auto ResolveRelativePath(
    std::filesystem::path const& path) noexcept -> std::filesystem::path {
    auto normalized = path.lexically_normal();
    return (normalized / "").parent_path();  // strip trailing slash
}

[[nodiscard]] auto LookupEntryPyPath(
    GitTree const& tree,
    std::filesystem::path::const_iterator it,
    std::filesystem::path::const_iterator const& end,
    bool ignore_special = false) noexcept -> GitTreeEntryPtr {
    auto segment = *it;
    auto entry = tree.LookupEntryByName(segment);
    if (not entry) {
        return nullptr;
    }
    if (++it != end) {
        auto const& subtree = entry->Tree(ignore_special);
        if (not subtree) {
            return nullptr;
        }
        return LookupEntryPyPath(*subtree, it, end);
    }
    return entry;
}

class SymlinksChecker final {
  public:
    explicit SymlinksChecker(gsl::not_null<GitCASPtr> const& cas) noexcept
        : cas_{*cas} {}

    [[nodiscard]] auto operator()(
        std::vector<ArtifactDigest> const& ids) const noexcept -> bool {
        return std::all_of(
            ids.begin(), ids.end(), [&cas = cas_](ArtifactDigest const& id) {
                return cas
                    .ReadObject(id.hash(),
                                /*is_hex_id=*/true,
                                /*as_valid_symlink=*/true)
                    .has_value();
            });
    };

  private:
    GitCAS const& cas_;
};

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
                   std::string const& tree_id,
                   bool ignore_special,
                   bool skip_checks) noexcept -> std::optional<GitTree> {
    if (auto raw_id = FromHexString(tree_id)) {
        auto repo = GitRepo::Open(cas);
        if (repo != std::nullopt) {
            auto entries =
                skip_checks ? repo->ReadDirectTree(
                                  *raw_id, /*is_hex_id=*/false, ignore_special)
                            : repo->ReadTree(*raw_id,
                                             SymlinksChecker{cas},
                                             /*is_hex_id=*/false,
                                             ignore_special);
            if (entries) {
                // NOTE: the raw_id value is NOT recomputed when
                // ignore_special==true.
                return GitTree::FromEntries(
                    cas, std::move(*entries), *raw_id, ignore_special);
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
            LogLevel::Debug, "git tree does not contain entry {}", name);
        return nullptr;
    }
    return entry_it->second;
}

auto GitTree::LookupEntryByPath(
    std::filesystem::path const& path) const noexcept -> GitTreeEntryPtr {
    auto resolved = ResolveRelativePath(path);
    return LookupEntryPyPath(
        *this, resolved.begin(), resolved.end(), ignore_special_);
}

auto GitTree::Size() const noexcept -> std::optional<std::size_t> {
    if (auto header = cas_->ReadHeader(raw_id_, /*is_hex_id=*/false)) {
        return header->first;
    }
    return std::nullopt;
}

auto GitTree::RawData() const noexcept -> std::optional<std::string> {
    return cas_->ReadObject(raw_id_, /*is_hex_id=*/false);
}

auto GitTreeEntry::Blob() const noexcept -> std::optional<std::string> {
    if (not IsBlob()) {
        return std::nullopt;
    }
    // return only valid blobs
    return cas_->ReadObject(raw_id_,
                            /*is_hex_id=*/false,
                            /*as_valid_symlink=*/IsSymlinkObject(type_));
}

auto GitTreeEntry::Tree(bool ignore_special) const& noexcept
    -> std::optional<GitTree> const& {
    return tree_cached_.SetOnceAndGet(
        [this, ignore_special]() -> std::optional<GitTree> {
            if (IsTree()) {
                auto repo = GitRepo::Open(cas_);
                if (repo == std::nullopt) {
                    return std::nullopt;
                }
                auto entries = repo->ReadTree(raw_id_,
                                              SymlinksChecker{cas_},
                                              /*is_hex_id=*/false,
                                              ignore_special);
                if (entries) {
                    // NOTE: the raw_id value is NOT recomputed when
                    // ignore_special==true.
                    return GitTree::FromEntries(
                        cas_, std::move(*entries), raw_id_, ignore_special);
                }
            }
            return std::nullopt;
        });
}

auto GitTreeEntry::Size() const noexcept -> std::optional<std::size_t> {
    if (auto header = cas_->ReadHeader(raw_id_, /*is_hex_id=*/false)) {
        return header->first;
    }
    return std::nullopt;
}

auto GitTreeEntry::RawData() const noexcept -> std::optional<std::string> {
    return cas_->ReadObject(raw_id_, /*is_hex_id=*/false);
}
