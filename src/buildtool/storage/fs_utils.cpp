// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/storage/fs_utils.hpp"

#include <unordered_map>
#include <utility>

#include "nlohmann/json.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/path.hpp"

namespace StorageUtils {

auto GetGitRoot(LocalPathsPtr const& just_mr_paths,
                std::string const& repo_url) noexcept -> std::filesystem::path {
    if (just_mr_paths->git_checkout_locations.contains(repo_url)) {
        if (just_mr_paths->git_checkout_locations[repo_url].is_string()) {
            return std::filesystem::absolute(ToNormalPath(std::filesystem::path{
                just_mr_paths->git_checkout_locations[repo_url]
                    .get<std::string>()}));
        }
        Logger::Log(
            LogLevel::Warning,
            "Retrieving Git checkout location: key {} has non-string value {}",
            nlohmann::json(repo_url).dump(),
            just_mr_paths->git_checkout_locations[repo_url].dump());
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

auto GetCommitTreeIDFile(std::string const& commit) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "commit-tree-map" / commit;
}

auto GetArchiveTreeIDFile(std::string const& repo_type,
                          std::string const& content) noexcept
    -> std::filesystem::path {
    return StorageConfig::BuildRoot() / "tree-map" / repo_type / content;
}

auto GetForeignFileTreeIDFile(std::string const& content,
                              std::string const& name,
                              bool executable) noexcept
    -> std::filesystem::path {
    return GetDistdirTreeIDFile(
        HashFunction::ComputeBlobHash(
            nlohmann::json(
                std::unordered_map<std::string, std::pair<std::string, bool>>{
                    {name, {content, executable}}})
                .dump())
            .HexString());
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
                      LocalPathsPtr const& just_mr_paths) noexcept {
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

}  // namespace StorageUtils
