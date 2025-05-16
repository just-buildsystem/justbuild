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
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "fmt/core.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"
#include "src/buildtool/execution_api/local/local_action.hpp"
#include "src/buildtool/execution_api/local/local_cas_reader.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
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
    auto StdErrDigest() noexcept -> std::optional<ArtifactDigest> final {
        auto digest = ArtifactDigestFactory::FromBazel(
            storage_.GetHashFunction().GetType(),
            output_.action.stderr_digest());
        if (digest) {
            return *digest;
        }
        return std::nullopt;
    }
    auto StdOutDigest() noexcept -> std::optional<ArtifactDigest> final {
        auto digest = ArtifactDigestFactory::FromBazel(
            storage_.GetHashFunction().GetType(),
            output_.action.stdout_digest());
        if (digest) {
            return *digest;
        }
        return std::nullopt;
    }
    auto ExitCode() const noexcept -> int final {
        return output_.action.exit_code();
    }
    auto IsCached() const noexcept -> bool final { return output_.is_cached; };

    auto ExecutionDuration() noexcept -> double final {
        return output_.duration;
    }

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

    auto HasUpwardsSymlinks() noexcept -> expected<bool, std::string> final {
        if (auto error_msg = Populate()) {
            return unexpected{*std::move(error_msg)};
        }
        return has_upwards_symlinks_;
    }

  private:
    std::string action_id_;
    LocalAction::Output output_{};
    Storage const& storage_;
    ArtifactInfos artifacts_;
    DirSymlinks dir_symlinks_;
    bool has_upwards_symlinks_ = false;  // only tracked in compatible mode
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
                // in compatible mode: track upwards symlinks
                has_upwards_symlinks_ =
                    has_upwards_symlinks_ or
                    (not ProtocolTraits::IsNative(hash_type) and
                     not PathIsNonUpwards(link.target()));
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
                // in compatible mode: track upwards symlinks
                has_upwards_symlinks_ =
                    has_upwards_symlinks_ or
                    (not ProtocolTraits::IsNative(hash_type) and
                     not PathIsNonUpwards(link.target()));
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
                // in compatible mode: track upwards symlinks; requires one
                // directory traversal; other sources of errors should cause a
                // fail too, so it is ok to report all traversal errors as
                // if an invalid entry was found
                if (not has_upwards_symlinks_ and
                    not ProtocolTraits::IsNative(hash_type)) {
                    LocalCasReader reader{&storage_.CAS()};
                    auto valid_dir = reader.IsDirectoryValid(*digest);
                    if (not valid_dir) {
                        return std::move(valid_dir).error();
                    }
                    has_upwards_symlinks_ = not *valid_dir;
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
        populated_ = true;
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
