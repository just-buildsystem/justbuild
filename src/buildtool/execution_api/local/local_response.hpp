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
#include <optional>
#include <string>
#include <utility>

#include "fmt/core.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/common/tree_reader.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/path.hpp"

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
        if (auto content = ReadContent(output_.action.stderr_digest())) {
            return *std::move(content);
        }
        Logger::Log(LogLevel::Debug, "reading stderr failed");
        return {};
    }
    auto StdOut() noexcept -> std::string final {
        if (auto content = ReadContent(output_.action.stdout_digest())) {
            return *std::move(content);
        }
        Logger::Log(LogLevel::Debug, "reading stdout failed");
        return {};
    }
    auto ExitCode() const noexcept -> int final {
        return output_.action.exit_code();
    }
    auto IsCached() const noexcept -> bool final { return output_.is_cached; };

    auto ActionDigest() const noexcept -> std::string const& final {
        return action_id_;
    }

    auto Artifacts() noexcept
        -> expected<gsl::not_null<ArtifactInfos const*>, std::string> final {
        if (auto error_msg = Populate()) {
            return unexpected{*std::move(error_msg)};
        }
        return gsl::not_null<ArtifactInfos const*>(
            &artifacts_);  // explicit type needed for expected
    }

    auto DirectorySymlinks() noexcept
        -> expected<gsl::not_null<DirSymlinks const*>, std::string> final {
        if (auto error_msg = Populate()) {
            return unexpected{*std::move(error_msg)};
        }
        return gsl::not_null<DirSymlinks const*>(
            &dir_symlinks_);  // explicit type needed for expected
    }

  private:
    std::string action_id_{};
    LocalAction::Output output_{};
    Storage const& storage_;
    ArtifactInfos artifacts_;
    DirSymlinks dir_symlinks_;
    bool populated_ = false;

    explicit LocalResponse(
        std::string action_id,
        LocalAction::Output output,
        gsl::not_null<Storage const*> const& storage) noexcept
        : action_id_{std::move(action_id)},
          output_{std::move(output)},
          storage_{*storage} {}

    /// \brief Populates the stored data, once.
    /// \returns Error message on failure, nullopt on success.
    [[nodiscard]] auto Populate() noexcept -> std::optional<std::string> {
        // Initialized only once lazily
        if (populated_) {
            return std::nullopt;
        }
        populated_ = true;

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
        dir_symlinks.reserve(static_cast<std::size_t>(
            action_result.output_directory_symlinks_size()));

        auto const hash_type = storage_.GetHashFunction().GetType();
        // collect files and store them
        for (auto const& file : action_result.output_files()) {
            auto digest =
                ArtifactDigestFactory::FromBazel(hash_type, file.digest());
            if (not digest) {
                return fmt::format(
                    "LocalResponse: failed to create artifact digest for {}",
                    file.path());
            }
            try {
                artifacts.emplace(
                    file.path(),
                    Artifact::ObjectInfo{.digest = *std::move(digest),
                                         .type = file.is_executable()
                                                     ? ObjectType::Executable
                                                     : ObjectType::File});
            } catch (std::exception const& ex) {
                return fmt::format(
                    "LocalResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    file.path(),
                    ex.what());
            }
        }

        // collect all symlinks and store them
        for (auto const& link : action_result.output_file_symlinks()) {
            try {
                // in compatible mode: check symlink validity
                if (not ProtocolTraits::IsNative(
                        storage_.GetHashFunction().GetType()) and
                    not PathIsNonUpwards(link.target())) {
                    return fmt::format(
                        "LocalResponse: found invalid symlink at {}",
                        link.path());
                }
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest =
                            ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                                storage_.GetHashFunction(), link.target()),
                        .type = ObjectType::Symlink});
            } catch (std::exception const& ex) {
                return fmt::format(
                    "LocalResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    link.path(),
                    ex.what());
            }
        }
        for (auto const& link : action_result.output_directory_symlinks()) {
            try {
                // in compatible mode: check symlink validity
                if (not ProtocolTraits::IsNative(
                        storage_.GetHashFunction().GetType()) and
                    not PathIsNonUpwards(link.target())) {
                    return fmt::format(
                        "LocalResponse: found invalid symlink at {}",
                        link.path());
                }
                artifacts.emplace(
                    link.path(),
                    Artifact::ObjectInfo{
                        .digest =
                            ArtifactDigestFactory::HashDataAs<ObjectType::File>(
                                storage_.GetHashFunction(), link.target()),
                        .type = ObjectType::Symlink});
                dir_symlinks.emplace(link.path());  // add it to set
            } catch (std::exception const& ex) {
                return fmt::format(
                    "LocalResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    link.path(),
                    ex.what());
            }
        }

        // collect directories and store them
        for (auto const& dir : action_result.output_directories()) {
            auto digest =
                ArtifactDigestFactory::FromBazel(hash_type, dir.tree_digest());
            if (not digest) {
                return fmt::format(
                    "LocalResponse: failed to create artifact digest for {}",
                    dir.path());
            }
            try {
                // in compatible mode: check validity of symlinks in dir
                if (not ProtocolTraits::IsNative(
                        storage_.GetHashFunction().GetType())) {
                    auto reader = TreeReader<LocalCasReader>{&storage_.CAS()};
                    auto result = reader.RecursivelyReadTreeLeafs(
                        *digest, "", /*include_trees=*/true);
                    if (not result) {
                        return fmt::format(
                            "LocalResponse: found invalid entries in directory "
                            "{}",
                            dir.path());
                    }
                }
                artifacts.emplace(
                    dir.path(),
                    Artifact::ObjectInfo{.digest = *std::move(digest),
                                         .type = ObjectType::Tree});
            } catch (std::exception const& ex) {
                return fmt::format(
                    "LocalResponse: unexpected failure gathering digest for "
                    "{}:\n{}",
                    dir.path(),
                    ex.what());
            }
        }
        artifacts_ = std::move(artifacts);
        dir_symlinks_ = std::move(dir_symlinks);
        return std::nullopt;
    }

    [[nodiscard]] auto ReadContent(bazel_re::Digest const& digest)
        const noexcept -> std::optional<std::string> {
        auto const a_digest = ArtifactDigestFactory::FromBazel(
            storage_.GetHashFunction().GetType(), digest);
        if (not a_digest) {
            return std::nullopt;
        }
        auto const path =
            storage_.CAS().BlobPath(*a_digest, /*is_executable=*/false);
        if (not path) {
            return std::nullopt;
        }
        return FileSystemManager::ReadFile(*path);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_RESPONSE_HPP
