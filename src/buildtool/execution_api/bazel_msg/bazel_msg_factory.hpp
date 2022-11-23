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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_common.hpp"
#include "src/buildtool/execution_api/bazel_msg/directory_tree.hpp"
#include "src/buildtool/execution_engine/dag/dag.hpp"
#include "src/buildtool/file_system/git_cas.hpp"

/// \brief Factory for creating Bazel API protobuf messages.
/// Responsible for creating protobuf messages necessary for Bazel API server
/// communication.
class BazelMsgFactory {
  public:
    using BlobStoreFunc = std::function<void(BazelBlob&&)>;
    using InfoStoreFunc = std::function<bool(std::filesystem::path const&,
                                             Artifact::ObjectInfo const&)>;
    using FileStoreFunc = std::function<
        std::optional<bazel_re::Digest>(std::filesystem::path const&, bool)>;
    using DirStoreFunc = std::function<std::optional<bazel_re::Digest>(
        std::string const&,
        bazel_re::Directory const&)>;
    using TreeStoreFunc = std::function<std::optional<bazel_re::Digest>(
        std::string const&,
        GitCAS::tree_entries_t const&)>;

    /// \brief Read object infos from directory.
    /// \returns true on success.
    [[nodiscard]] static auto ReadObjectInfosFromDirectory(
        bazel_re::Directory const& dir,
        InfoStoreFunc const& store_info) noexcept -> bool;

    /// \brief Read object infos from git tree.
    /// \returns true on success.
    [[nodiscard]] static auto ReadObjectInfosFromGitTree(
        GitCAS::tree_entries_t const& entries,
        InfoStoreFunc const& store_info) noexcept -> bool;

    /// \brief Create Directory digest from artifact tree structure.
    /// Recursively traverse entire tree and create blobs for sub-directories.
    /// \param tree         Directory tree of artifacts.
    /// \param store_blob   Function for storing Directory blobs.
    /// \param store_info   Function for storing object infos.
    /// \returns Digest representing the entire tree.
    [[nodiscard]] static auto CreateDirectoryDigestFromTree(
        DirectoryTreePtr const& tree,
        std::optional<BlobStoreFunc> const& store_blob = std::nullopt,
        std::optional<InfoStoreFunc> const& store_info = std::nullopt) noexcept
        -> std::optional<bazel_re::Digest>;

    /// \brief Create Directory digest from local file root.
    /// Recursively traverse entire root and store files and directories.
    /// \param root         Path to local file root.
    /// \param store_file   Function for storing local file via path.
    /// \param store_dir    Function for storing Directory blobs.
    /// \returns Digest representing the entire file root.
    [[nodiscard]] static auto CreateDirectoryDigestFromLocalTree(
        std::filesystem::path const& root,
        FileStoreFunc const& store_file,
        DirStoreFunc const& store_dir) noexcept
        -> std::optional<bazel_re::Digest>;

    /// \brief Create Git tree digest from local file root.
    /// Recursively traverse entire root and store files and directories.
    /// \param root         Path to local file root.
    /// \param store_file   Function for storing local file via path.
    /// \param store_tree   Function for storing git trees.
    /// \returns Digest representing the entire file root.
    [[nodiscard]] static auto CreateGitTreeDigestFromLocalTree(
        std::filesystem::path const& root,
        FileStoreFunc const& store_file,
        TreeStoreFunc const& store_tree) noexcept
        -> std::optional<bazel_re::Digest>;

    /// \brief Creates Action digest from command line.
    /// As part of the internal process, it creates an ActionBundle and
    /// CommandBundle that can be captured via BlobStoreFunc.
    /// \param[in] cmdline      The command line.
    /// \param[in] exec_dir     The Digest of the execution directory.
    /// \param[in] output_files The paths of output files.
    /// \param[in] output_dirs  The paths of output directories.
    /// \param[in] output_node. The output node's properties.
    /// \param[in] env_vars     The environment variables set.
    /// \param[in] properties   The target platform's properties.
    /// \param[in] do_not_cache Skip action cache.
    /// \param[in] timeout      The command execution timeout.
    /// \param[in] store_blob Function for storing action and cmd bundles.
    /// \returns Digest representing the action.
    [[nodiscard]] static auto CreateActionDigestFromCommandLine(
        std::vector<std::string> const& cmdline,
        bazel_re::Digest const& exec_dir,
        std::vector<std::string> const& output_files,
        std::vector<std::string> const& output_dirs,
        std::vector<std::string> const& output_node_properties,
        std::vector<bazel_re::Command_EnvironmentVariable> const& env_vars,
        std::vector<bazel_re::Platform_Property> const& properties,
        bool do_not_cache,
        std::chrono::milliseconds const& timeout,
        std::optional<BlobStoreFunc> const& store_blob = std::nullopt)
        -> bazel_re::Digest;

    /// \brief Create descriptive string from Directory protobuf message.
    [[nodiscard]] static auto DirectoryToString(
        bazel_re::Directory const& dir) noexcept -> std::optional<std::string>;

    /// \brief Create descriptive string from Git tree entries.
    [[nodiscard]] static auto GitTreeToString(
        GitCAS::tree_entries_t const& entries) noexcept
        -> std::optional<std::string>;

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

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_MSG_FACTORY_HPP
