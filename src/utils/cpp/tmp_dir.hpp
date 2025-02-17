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

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

class TmpFile;

class TmpDir final {
  public:
    using Ptr = std::shared_ptr<TmpDir const>;

    TmpDir(TmpDir const&) = delete;
    auto operator=(TmpDir const&) -> TmpDir& = delete;
    TmpDir(TmpDir&& other) = delete;
    auto operator=(TmpDir&&) -> TmpDir& = delete;
    ~TmpDir() noexcept;

    [[nodiscard]] auto GetPath() const& noexcept
        -> std::filesystem::path const& {
        return tmp_dir_;
    }

    /// \brief Creates a completely unique directory in a given prefix path.
    [[nodiscard]] static auto Create(
        std::filesystem::path const& prefix) noexcept -> Ptr;

    /// \brief Create a new nested temporary directory. This nested directory
    /// remains valid even if the parent directory goes out of scope.
    [[nodiscard]] static auto CreateNestedDirectory(
        TmpDir::Ptr const& parent) noexcept -> Ptr;

    /// \brief Create a new unique temporary file. To guarantee uniqueness,
    /// every file gets created in a new temporary directory. This file remains
    /// valid even if the parent directory goes out of scope.
    [[nodiscard]] static auto CreateFile(
        TmpDir::Ptr const& parent,
        std::string const& file_name = "file") noexcept
        -> std::shared_ptr<TmpFile const>;

  private:
    explicit TmpDir(TmpDir::Ptr parent, std::filesystem::path path) noexcept
        : parent_{std::move(parent)}, tmp_dir_{std::move(path)} {}

    [[nodiscard]] static auto CreateImpl(
        TmpDir::Ptr parent,
        std::filesystem::path const& path) noexcept -> Ptr;

    TmpDir::Ptr parent_;
    std::filesystem::path tmp_dir_;
};

class TmpFile final {
    friend class TmpDir;

  public:
    using Ptr = std::shared_ptr<TmpFile const>;

    TmpFile(TmpFile const&) = delete;
    auto operator=(TmpFile const&) -> TmpFile& = delete;
    TmpFile(TmpFile&& other) = delete;
    auto operator=(TmpFile&&) -> TmpFile& = delete;
    ~TmpFile() noexcept = default;

    [[nodiscard]] auto GetPath() const& noexcept
        -> std::filesystem::path const& {
        return file_path_;
    }

  private:
    explicit TmpFile(TmpDir::Ptr parent,
                     std::filesystem::path file_path) noexcept
        : parent_{std::move(parent)}, file_path_{std::move(file_path)} {}

    TmpDir::Ptr parent_;
    std::filesystem::path file_path_;
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_TMP_DIR_HPP
