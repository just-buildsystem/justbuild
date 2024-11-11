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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_MSG_FACTORY_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_MSG_FACTORY_HPP

#include <chrono>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_api/common/artifact_blob_container.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"

/// \brief Factory for creating Bazel API protobuf messages.
/// Responsible for creating protobuf messages necessary for Bazel API server
/// communication.
class BazelMsgFactory {
  public:
    /// \brief Store or otherwise process a blob. Returns success flag.
    using BlobProcessFunc = std::function<bool(ArtifactBlob&&)>;
    using LinkDigestResolveFunc =
        std::function<void(std::vector<ArtifactDigest> const&,
                           gsl::not_null<std::vector<std::string>*> const&)>;
    using GitReadFunc = std::function<std::optional<
        std::variant<std::filesystem::path, std::string>>(ArtifactDigest const&,
                                                          ObjectType)>;
    using BlobStoreFunc = std::function<std::optional<ArtifactDigest>(
        std::variant<std::filesystem::path, std::string> const&,
        bool)>;
    using FileStoreFunc = std::function<
        std::optional<ArtifactDigest>(std::filesystem::path const&, bool)>;
    using SymlinkStoreFunc =
        std::function<std::optional<ArtifactDigest>(std::string const&)>;
    using TreeStoreFunc =
        std::function<std::optional<ArtifactDigest>(std::string const&)>;
    using RehashedDigestReadFunc =
        std::function<expected<std::optional<Artifact::ObjectInfo>,
                               std::string>(ArtifactDigest const&)>;
    using RehashedDigestStoreFunc =
        std::function<std::optional<std::string>(ArtifactDigest const&,
                                                 ArtifactDigest const&,
                                                 ObjectType)>;

    /// \brief Create Directory digest from artifact tree structure. Uses
    /// compatible HashFunction for hashing. Recursively traverse entire tree
    /// and create blobs for sub-directories.
    /// \param tree           Directory tree of artifacts.
    /// \param resolve_links  Function for resolving symlinks.
    /// \param process_blob   Function for processing Directory blobs.
    /// \returns Digest representing the entire tree.
    [[nodiscard]] static auto CreateDirectoryDigestFromTree(
        DirectoryTreePtr const& tree,
        LinkDigestResolveFunc const& resolve_links,
        BlobProcessFunc const& process_blob) noexcept
        -> std::optional<ArtifactDigest>;

    /// \brief Create Directory digest from an owned Git tree.
    /// Recursively traverse entire tree and store files and directories.
    /// Used to convert from native to compatible representation of trees.
    /// \param digest       Digest of a Git tree.
    /// \param read_git     Function for reading Git tree entries. Reading from
    ///                     CAS returns the CAS path, while reading from Git CAS
    ///                     returns content directly. This differentiation is
    ///                     made to avoid unnecessary storing blobs in memory.
    /// \param store_file   Function for storing file via path or content.
    /// \param store_dir    Function for storing Directory blobs.
    /// \param store_symlink    Function for storing symlink via content.
    /// \param read_rehashed    Function to read mapping between digests.
    /// \param store_rehashed   Function to store mapping between digests.
    /// \returns Digest representing the entire tree directory, or error string
    /// on failure.
    [[nodiscard]] static auto CreateDirectoryDigestFromGitTree(
        ArtifactDigest const& digest,
        GitReadFunc const& read_git,
        BlobStoreFunc const& store_file,
        TreeStoreFunc const& store_dir,
        SymlinkStoreFunc const& store_symlink,
        RehashedDigestReadFunc const& read_rehashed,
        RehashedDigestStoreFunc const& store_rehashed) noexcept
        -> expected<ArtifactDigest, std::string>;

    /// \brief Create Directory digest from local file root.
    /// Recursively traverse entire root and store files and directories.
    /// \param root         Path to local file root.
    /// \param store_file   Function for storing local file via path.
    /// \param store_dir    Function for storing Directory blobs.
    /// \param store_symlink  Function for storing symlink via content.
    /// \returns Digest representing the entire file root.
    [[nodiscard]] static auto CreateDirectoryDigestFromLocalTree(
        std::filesystem::path const& root,
        FileStoreFunc const& store_file,
        TreeStoreFunc const& store_dir,
        SymlinkStoreFunc const& store_symlink) noexcept
        -> std::optional<ArtifactDigest>;

    /// \brief Create Git tree digest from local file root.
    /// Recursively traverse entire root and store files and directories.
    /// \param root         Path to local file root.
    /// \param store_file   Function for storing local file via path.
    /// \param store_tree   Function for storing git trees.
    /// \param store_symlink  Function for storing symlink via content.
    /// \returns Digest representing the entire file root.
    [[nodiscard]] static auto CreateGitTreeDigestFromLocalTree(
        std::filesystem::path const& root,
        FileStoreFunc const& store_file,
        TreeStoreFunc const& store_tree,
        SymlinkStoreFunc const& store_symlink) noexcept
        -> std::optional<ArtifactDigest>;

    struct ActionDigestRequest;
    /// \brief Creates Action digest from command line.
    /// As part of the internal process, it creates an ActionBundle and
    /// CommandBundle that can be captured via BlobStoreFunc.
    /// \returns Digest representing the action.
    [[nodiscard]] static auto CreateActionDigestFromCommandLine(
        ActionDigestRequest const& request) -> std::optional<ArtifactDigest>;

    /// \brief Create message vector from std::map.
    /// \param[in]  input   map
    /// \tparam     T       protobuf message type. It must be a name-value
    /// message (i.e. class methods T::set_name(std::string) and
    /// T::set_value(std::string) must exist)
    template <class T>
    [[nodiscard]] static auto CreateMessageVectorFromMap(
        std::map<std::string, std::string> const& input) noexcept
        -> std::vector<T> {
        std::vector<T> output{};
        std::transform(std::begin(input),
                       std::end(input),
                       std::back_inserter(output),
                       [](auto const& key_val) {
                           T msg;
                           msg.set_name(key_val.first);
                           msg.set_value(key_val.second);
                           return msg;
                       });
        return output;
    }

    template <class T>
    [[nodiscard]] static auto MessageFromString(std::string const& blob)
        -> std::optional<T> {
        T msg{};
        if (msg.ParseFromString(blob)) {
            return msg;
        }
        Logger::Log(LogLevel::Error, "failed to parse message from string");
        return std::nullopt;
    }
};

struct BazelMsgFactory::ActionDigestRequest final {
    using BlobStoreFunc = std::function<void(BazelBlob&&)>;

    template <typename T>
    using VectorPtr = gsl::not_null<std::vector<T> const*>;

    /// \brief The command line.
    VectorPtr<std::string> const command_line;

    /// \brief The workingg direcotry
    gsl::not_null<std::string const*> const cwd;

    /// \brief The paths of output files.
    VectorPtr<std::string> const output_files;

    /// \brief The paths of output directories.
    VectorPtr<std::string> const output_dirs;

    /// \brief The environment variables set.
    VectorPtr<bazel_re::Command_EnvironmentVariable> const env_vars;

    /// \brief The target platform's properties.
    VectorPtr<bazel_re::Platform_Property> const properties;

    /// \brief The Digest of the execution directory.
    gsl::not_null<ArtifactDigest const*> const exec_dir;

    /// \brief Hash function to be used.
    HashFunction const hash_function;

    /// \brief The command execution timeout.
    std::chrono::milliseconds const timeout;

    /// \brief Skip action cache.
    bool skip_action_cache;

    /// \brief Function for storing action and cmd bundles.
    std::optional<BlobStoreFunc> const store_blob = std::nullopt;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_MSG_FACTORY_HPP
