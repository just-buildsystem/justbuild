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

#include "src/other_tools/just_mr/utils.hpp"

#include "src/buildtool/file_system/file_storage.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/path.hpp"

namespace JustMR::Utils {

auto GetGitRoot(JustMR::PathsPtr const& just_mr_paths,
                std::string const& repo_url) noexcept -> std::filesystem::path {
    if (just_mr_paths->git_checkout_locations.contains(repo_url)) {
        return std::filesystem::absolute(ToNormalPath(std::filesystem::path{
            just_mr_paths->git_checkout_locations[repo_url]
                .get<std::string>()}));
    }
    auto repo_url_as_path = std::filesystem::absolute(
        ToNormalPath(std::filesystem::path(repo_url)));
    if (not repo_url_as_path.empty() and
        FileSystemManager::IsAbsolutePath(repo_url_as_path) and
        FileSystemManager::IsDirectory(repo_url_as_path)) {
        return repo_url_as_path;
    }
    return StorageConfig::GitRoot();
}

auto CreateTypedTmpDir(std::string const& type) noexcept -> TmpDirPtr {
    // try to create parent dir
    auto parent_path =
        StorageConfig::GenerationCacheRoot(0) / "tmp-workspaces" / type;
    return TmpDir::Create(parent_path);
}

auto GetCommitTreeIDFile(std::string const& commit) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "commit-tree-map" / commit;
}

auto GetArchiveTreeIDFile(std::string const& repo_type,
                          std::string const& content) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "tree-map" / repo_type / content;
}

auto GetDistdirTreeIDFile(std::string const& content) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "distfiles-tree-map" / content;
}

auto GetResolvedTreeIDFile(std::string const& tree_hash,
                           PragmaSpecial const& pragma_special) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "special-tree-map" /
           kPragmaSpecialInverseMap.at(pragma_special) / tree_hash;
}

auto WriteTreeIDFile(std::filesystem::path const& tree_id_file,
                     std::string const& tree_id) noexcept -> bool {
    // needs to be done safely, so use the rename trick
    auto tmp_dir = TmpDir::Create(tree_id_file.parent_path());
    if (not tmp_dir) {
        Logger::Log(LogLevel::Error,
                    "could not create tmp dir for writing tree id file {}",
                    tree_id_file.string());
        return false;
    }
    std::filesystem::path tmp_file{tmp_dir->GetPath() / "tmp_file"};
    if (not FileSystemManager::WriteFile(tree_id, tmp_file)) {
        Logger::Log(LogLevel::Error, "could not create tmp tree id file");
        return false;
    }
    return FileSystemManager::Rename(tmp_file.string(), tree_id_file);
}

auto AddToCAS(std::string const& data) noexcept
    -> std::optional<std::filesystem::path> {
    // get file CAS instance
    auto const& cas = Storage::Instance().CAS();
    // write to cas
    auto digest = cas.StoreBlob(data);
    if (digest) {
        return cas.BlobPath(*digest, /*is_executable=*/false);
    }
    return std::nullopt;
}

void AddDistfileToCAS(std::filesystem::path const& distfile,
                      JustMR::PathsPtr const& just_mr_paths) noexcept {
    auto const& cas = Storage::Instance().CAS();
    for (auto const& dirpath : just_mr_paths->distdirs) {
        auto candidate = dirpath / distfile;
        if (FileSystemManager::Exists(candidate)) {
            // try to add to CAS
            [[maybe_unused]] auto digest =
                cas.StoreBlob(candidate, /*is_executable=*/false);
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos,
                 gsl::not_null<std::unordered_set<std::string>*> const& seen)
    -> std::optional<ExpressionPtr> {
    if (not repo_desc->IsString()) {
        return repo_desc;
    }
    auto desc_str = repo_desc->String();
    if (seen->contains(desc_str)) {
        // cyclic dependency
        return std::nullopt;
    }
    [[maybe_unused]] auto insert_res = seen->insert(desc_str);
    return ResolveRepo(repos[desc_str]["repository"], repos, seen);
}

auto ResolveRepo(ExpressionPtr const& repo_desc,
                 ExpressionPtr const& repos) noexcept
    -> std::optional<ExpressionPtr> {
    std::unordered_set<std::string> seen = {};
    try {
        return ResolveRepo(repo_desc, repos, &seen);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error,
                    "Config: While resolving dependencies: {}",
                    e.what());
        return std::nullopt;
    }
}

}  // namespace JustMR::Utils
