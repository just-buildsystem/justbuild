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

#ifndef INCLUDED_SRC_TEST_UTILS_HERMETICITY_TEST_STORAGE_CONFIG_HPP
#define INCLUDED_SRC_TEST_UTILS_HERMETICITY_TEST_STORAGE_CONFIG_HPP

#include <cstdlib>  //std::exit, std::getenv
#include <filesystem>
#include <string>
#include <utility>  //std::move

#include "gsl/gsl"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/utils/cpp/tmp_dir.hpp"

class TestStorageConfig final {
  public:
    /// \brief Create a unique StorageConfig that has the build root in a new
    /// empty location. Uses TEST_TMPDIR environment variable to determine path
    /// to the location.
    /// To be used only for local tests, as it does not know about remote
    /// execution config.
    [[nodiscard]] static auto Create() noexcept -> TestStorageConfig {
        /**
         * Test must not assume the existence of a home directory, nor write
         * there. Hence we set the storage root to a fixed location under
         * TEST_TMPDIR which is set by the test launcher.
         */
        auto const test_tempdir =
            std::filesystem::path{std::string{std::getenv("TEST_TMPDIR")}} /
            ".test_build_root";

        auto temp_dir = TmpDir::Create(test_tempdir);
        if (temp_dir == nullptr) {
            Logger::Log(LogLevel::Error,
                        "failed to create a test-local cache dir");
            std::exit(EXIT_FAILURE);
        }

        StorageConfig::Builder builder;
        auto config = builder.SetBuildRoot(temp_dir->GetPath())
                          .SetHashType(Compatibility::IsCompatible()
                                           ? HashFunction::Type::PlainSHA256
                                           : HashFunction::Type::GitSHA1)
                          .Build();
        if (not config) {
            Logger::Log(LogLevel::Error, config.error());
            std::exit(EXIT_FAILURE);
        }
        Logger::Log(LogLevel::Debug,
                    "created test-local cache dir {}",
                    temp_dir->GetPath().string());
        return TestStorageConfig{std::move(temp_dir), *std::move(config)};
    }

    [[nodiscard]] auto Get() const noexcept -> StorageConfig const& {
        return storage_config_;
    }

  private:
    gsl::not_null<TmpDirPtr> const tmp_dir_;
    StorageConfig const storage_config_;

    explicit TestStorageConfig(gsl::not_null<TmpDirPtr> const& tmp_dir,
                               StorageConfig config) noexcept
        : tmp_dir_{tmp_dir}, storage_config_{std::move(config)} {}
};

#endif  // INCLUDED_SRC_TEST_UTILS_HERMETICITY_TEST_STORAGE_CONFIG_HPP
