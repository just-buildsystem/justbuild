// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_READER_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/storage/local_cas.hpp"

class LocalCasReader final {
  public:
    using DumpCallback = std::function<bool(std::string const&)>;

    explicit LocalCasReader(
        gsl::not_null<LocalCAS<true> const*> const& cas) noexcept
        : cas_(*cas) {}

    [[nodiscard]] auto ReadDirectory(ArtifactDigest const& digest)
        const noexcept -> std::optional<bazel_re::Directory>;

    [[nodiscard]] auto ReadGitTree(ArtifactDigest const& digest) const noexcept
        -> std::optional<GitRepo::tree_entries_t>;

    [[nodiscard]] auto DumpRawTree(Artifact::ObjectInfo const& info,
                                   DumpCallback const& dumper) const noexcept
        -> bool;

    [[nodiscard]] auto DumpBlob(Artifact::ObjectInfo const& info,
                                DumpCallback const& dumper) const noexcept
        -> bool;

  private:
    LocalCAS<true> const& cas_;

    [[nodiscard]] static auto DumpRaw(std::filesystem::path const& path,
                                      DumpCallback const& dumper) noexcept
        -> bool;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_CAS_READER_HPP
