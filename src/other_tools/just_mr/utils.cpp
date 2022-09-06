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

#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/utils/cpp/path.hpp"

namespace JustMR::Utils {

auto GetGitCacheRoot() noexcept -> std::filesystem::path {
    return LocalExecutionConfig::BuildRoot() / "git";
}

auto GetGitRoot(JustMR::PathsPtr const& just_mr_paths,
                std::string const& repo_url) noexcept -> std::filesystem::path {
    if (just_mr_paths->git_checkout_locations.contains(repo_url)) {
        return std::filesystem::absolute(ToNormalPath(std::filesystem::path(
            just_mr_paths->git_checkout_locations[repo_url])));
    }
    auto repo_url_as_path = std::filesystem::absolute(
        ToNormalPath(std::filesystem::path(repo_url)));
    if (not repo_url_as_path.empty() and
        FileSystemManager::IsAbsolutePath(repo_url_as_path) and
        FileSystemManager::IsDirectory(repo_url_as_path)) {
        return repo_url_as_path;
    }
    return GetGitCacheRoot();
}

auto CreateTypedTmpDir(std::string const& type) noexcept -> TmpDirPtr {
    // try to create parent dir
    auto parent_path =
        LocalExecutionConfig::BuildRoot() / "tmp-workspaces" / type;
    return TmpDir::Create(parent_path);
}

auto GetArchiveTreeIDFile(std::string const& repo_type,
                          std::string const& content) noexcept
    -> std::filesystem::path {
    return LocalExecutionConfig::BuildRoot() / "tree-map" / repo_type / content;
}

auto GetDistdirTreeIDFile(std::string const& content) noexcept
    -> std::filesystem::path {
    return LocalExecutionConfig::BuildRoot() / "distfiles-tree-map" / content;
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
    auto const& casf = LocalCAS<ObjectType::File>::Instance();
    // write to casf
    auto digest = casf.StoreBlobFromBytes(data);
    if (digest) {
        return casf.BlobPath(*digest);
    }
    return std::nullopt;
}

void AddDistfileToCAS(std::filesystem::path const& distfile,
                      JustMR::PathsPtr const& just_mr_paths) noexcept {
    auto const& casf = LocalCAS<ObjectType::File>::Instance();
    for (auto const& dirpath : just_mr_paths->distdirs) {
        auto candidate = dirpath / distfile;
        if (FileSystemManager::Exists(candidate)) {
            // try to add to CAS
            [[maybe_unused]] auto digest = casf.StoreBlobFromFile(candidate);
        }
    }
}

}  // namespace JustMR::Utils
