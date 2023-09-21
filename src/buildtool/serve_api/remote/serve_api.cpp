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

#include "src/buildtool/serve_api/remote/serve_api.hpp"

ServeApi::ServeApi(std::string const& host, Port port) noexcept
    : stc_{std::make_unique<SourceTreeClient>(host, port)} {}

// implement move constructor in cpp, where all members are complete types
ServeApi::ServeApi(ServeApi&& other) noexcept = default;

// implement destructor in cpp, where all members are complete types
ServeApi::~ServeApi() = default;

auto ServeApi::RetrieveTreeFromCommit(std::string const& commit,
                                      std::string const& subdir,
                                      bool sync_tree)
    -> std::optional<std::string> {
    return stc_->ServeCommitTree(commit, subdir, sync_tree);
}

auto ServeApi::RetrieveTreeFromArchive(
    std::string const& content,
    std::string const& archive_type,
    std::string const& subdir,
    std::optional<PragmaSpecial> const& resolve_symlinks,
    bool sync_tree) -> std::optional<std::string> {
    return stc_->ServeArchiveTree(
        content, archive_type, subdir, resolve_symlinks, sync_tree);
}
