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

#include "src/utils/cpp/tmp_dir.hpp"

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto TmpDir::Create(std::filesystem::path const& prefix,
                    std::string const& dir_template) noexcept -> TmpDirPtr {
    try {
        // make sure prefix folder exists
        if (not FileSystemManager::CreateDirectory(prefix)) {
            Logger::Log(LogLevel::Error,
                        "TmpDir: could not create prefix directory {}",
                        prefix.string());
            return nullptr;
        }

        auto full_template_str =
            std::filesystem::weakly_canonical(
                std::filesystem::absolute(prefix / dir_template))
                .string();

        std::vector<char> c_tmpl(full_template_str.begin(),
                                 full_template_str.end());
        c_tmpl.emplace_back('\0');  // get as c-string

        // attempt to make the tmp dir
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
        char* tmp_dir = mkdtemp(c_tmpl.data());
        if (tmp_dir == nullptr) {
            return nullptr;
        }
        auto tmp_dir_obj = std::make_shared<TmpDir>();
        // need to explicitly transform to std::string first
        tmp_dir_obj->tmp_dir_ = std::filesystem::path(std::string(tmp_dir));

        return std::static_pointer_cast<TmpDir const>(tmp_dir_obj);
    } catch (...) {
        return nullptr;
    }
}

auto TmpDir::GetPath() const& noexcept -> std::filesystem::path const& {
    return tmp_dir_;
}

TmpDir::~TmpDir() noexcept {
    // try to remove the tmp dir and all its content
    [[maybe_unused]] auto res =
        FileSystemManager::RemoveDirectory(tmp_dir_, true);
}
