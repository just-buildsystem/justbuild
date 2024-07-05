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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_RESPONSE_HPP

#include <cstddef>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"

/// \brief Response of a LocalAction.
class LocalResponse final : public IExecutionResponse {
    friend class LocalAction;

  public:
    auto Status() const noexcept -> StatusCode final {
        return StatusCode::Success;  // unused
    }
    auto HasStdErr() const noexcept -> bool final {
        return (output_.action.stderr_digest().size_bytes() != 0);
    }
    auto HasStdOut() const noexcept -> bool final {
        return (output_.action.stdout_digest().size_bytes() != 0);
    }
    auto StdErr() noexcept -> std::string final {
        if (auto path = storage_.CAS().BlobPath(output_.action.stderr_digest(),
                                                /*is_executable=*/false)) {
            if (auto content = FileSystemManager::ReadFile(*path)) {
                return std::move(*content);
            }
        }
        Logger::Log(LogLevel::Debug, "reading stderr failed");
        return {};
    }
    auto StdOut() noexcept -> std::string final {
        if (auto path = storage_.CAS().BlobPath(output_.action.stdout_digest(),
                                                /*is_executable=*/false)) {
            if (auto content = FileSystemManager::ReadFile(*path)) {
                return std::move(*content);
            }
        }
        Logger::Log(LogLevel::Debug, "reading stdout failed");
        return {};
    }
    auto ExitCode() const noexcept -> int final {
        return output_.action.exit_code();
    }
    auto IsCached() const noexcept -> bool final { return output_.is_cached; };

    auto ActionDigest() const noexcept -> std::string final {
        return action_id_;
    }

    auto Artifacts() noexcept -> ArtifactInfos final {
        return ArtifactsWithDirSymlinks().first;
    }

    auto ArtifactsWithDirSymlinks() noexcept
        -> std::pair<ArtifactInfos, DirSymlinks> final {
        // make sure to populate only once
        populated_ = Populate();
        if (not populated_) {
            return {};
        }
        return std::pair(artifacts_, dir_symlinks_);
    };

  private:
    std::string action_id_{};
    LocalAction::Output output_{};
    Storage const& storage_;
    ArtifactInfos artifacts_{};
    DirSymlinks dir_symlinks_{};
    bool populated_{false};

    explicit LocalResponse(
        std::string action_id,
        LocalAction::Output output,
        gsl::not_null<Storage const*> const& storage) noexcept
        : action_id_{std::move(action_id)},
          output_{std::move(output)},
          storage_{*storage} {}

    [[nodiscard]] auto Populate() noexcept -> bool {
        if (populated_) {
            return true;
        }
        ArtifactInfos artifacts{};
        auto const& action_result = output_.action;
        artifacts.reserve(
            static_cast<std::size_t>(action_result.output_files_size()) +
            static_cast<std::size_t>(
                action_result.output_file_symlinks_size()) +
            static_cast<std::size_t>(
                action_result.output_directory_symlinks_size()) +
            static_cast<std::size_t>(action_result.output_directories_size()));

        DirSymlinks dir_symlinks{};
        dir_symlinks_.reserve(static_cast<std::size_t>(
            action_result.output_directory_symlinks_size()));

        // collect files and store them
        for (auto const& file : action_result.output_files()) {
            try {
                artifacts.emplace(
                    file.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest{file.digest()},
                        .type = file.is_executable() ? ObjectType::Executable
                                                     : ObjectType::File});
            } catch (...) {
                return false;
            }
        }

        // collect all symlinks and store them
        for (auto const& link : action_result.output_file_symlinks()) {
            try {
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest::Create<ObjectType::File>(
                            HashFunction::Instance(), link.target()),
                        .type = ObjectType::Symlink});
            } catch (...) {
                return false;
            }
        }
        for (auto const& link : action_result.output_directory_symlinks()) {
            try {
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest::Create<ObjectType::File>(
                            HashFunction::Instance(), link.target()),
                        .type = ObjectType::Symlink});
                dir_symlinks.emplace(link.path());  // add it to set
            } catch (...) {
                return false;
            }
        }

        // collect all symlinks and store them
        for (auto const& link : action_result.output_file_symlinks()) {
            try {
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest::Create<ObjectType::File>(
                            HashFunction::Instance(), link.target()),
                        .type = ObjectType::Symlink});
            } catch (...) {
                return false;
            }
        }
        for (auto const& link : action_result.output_directory_symlinks()) {
            try {
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest::Create<ObjectType::File>(
                            HashFunction::Instance(), link.target()),
                        .type = ObjectType::Symlink});
            } catch (...) {
                return false;
            }
        }

        // collect directories and store them
        for (auto const& dir : action_result.output_directories()) {
            try {
                artifacts.emplace(
                    dir.path(),
                    Artifact::ObjectInfo{
                        .digest = ArtifactDigest{dir.tree_digest()},
                        .type = ObjectType::Tree});
            } catch (...) {
                return false;
            }
        }
        artifacts_ = std::move(artifacts);
        dir_symlinks_ = std::move(dir_symlinks);
        return true;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_RESPONSE_HPP
