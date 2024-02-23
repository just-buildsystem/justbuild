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

#ifndef INCLUDED_SRC_OTHER_TOOLS_TMP_DIR_HPP
#define INCLUDED_SRC_OTHER_TOOLS_TMP_DIR_HPP

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

std::string const kDefaultTemplate{"tmp.XXXXXX"};

class TmpDir;
using TmpDirPtr = std::shared_ptr<TmpDir const>;

class TmpDir {
  public:
    // default ctor; not to be used!
    TmpDir() = default;

    /// \brief Destroy the TmpDir object. It tries to remove the tmp folder.
    ~TmpDir() noexcept;

    // no copies, no moves
    TmpDir(TmpDir const&) = delete;
    TmpDir(TmpDir&& other) noexcept = delete;
    auto operator=(TmpDir const&) = delete;
    auto operator=(TmpDir&& other) noexcept -> TmpDir& = delete;

    [[nodiscard]] auto GetPath() const& noexcept
        -> std::filesystem::path const&;
    [[nodiscard]] auto GetPath() && = delete;

    /// \brief Creates a completely unique directory in a given prefix path.
    [[nodiscard]] static auto Create(
        std::filesystem::path const& prefix,
        std::string const& dir_template = kDefaultTemplate) noexcept
        -> TmpDirPtr;

  private:
    std::filesystem::path tmp_dir_{};
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_TMP_DIR_HPP
