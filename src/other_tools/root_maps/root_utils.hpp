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

#ifndef INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP

#include <filesystem>
#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

/// \brief Calls the ServeApi to check whether the serve endpoint has the given
/// tree available to build against.
/// \param tree_id The Git-tree identifier.
/// \param logger An AsyncMapConsumer logger instance.
/// \returns Nullopt if an error in the ServeApi call ocurred, or a flag stating
/// whether the serve endpoint knows the tree on ServeApi call success. The
/// logger is called with fatal ONLY if this method returns nullopt.
[[nodiscard]] auto CheckServeHasAbsentRoot(
    ServeApi const& serve,
    std::string const& tree_id,
    AsyncMapConsumerLoggerPtr const& logger) -> std::optional<bool>;

/// \brief Calls the ServeApi to instruct the serve endpoint to set up a root
/// defined by a given tree by retrieving it from the remote CAS. This method
/// ensures the respective tree is in the remote CAS prior to the ServeApi call
/// by uploading it to the remote CAS if it is missing.
/// IMPORTANT: No check is performed for the compatibility mode of the protocol
/// used by given remote execution endpoint!
/// \param tree_id The Git-tree identifier.
/// \param repo_path Local witnessing Git repository for the tree.
/// \param native_storage_config Configuration of the native local storage.
/// \param compat_storage_config Optional configuration of the compatible local
/// storage, if it was set up.
/// \param compat_storage Optional compatible local storage, if it was set up.
/// \param local_api Optional API that knows how to communicate with the
/// remote-execution endpoint specified by parameter remote_api, if given. In
/// particular, it is expected to be provided if the remote is compatible.
/// \param remote_api Optional API of the remote-execution endpoint.
/// If nullopt, skip the upload to the remote CAS; this assumes prior knowledge
/// which guarantees the tree given by tree_id exists in the remote CAS for the
/// duration of the subsequent serve API call; this option should be used
/// carefully, but does result in less remote communication.
/// \param logger An AsyncMapConsumer logger instance.
/// \param no_sync_is_fatal If true, report only as a warning the failure of the
/// serve endpoint to set up the root for this tree; otherwise, this is reported
/// as fatal.
/// \returns Status flag, with false if state is deemed fatal, and true
/// otherwise. Logger is only called with fatal if returning false.
[[nodiscard]] auto EnsureAbsentRootOnServe(
    ServeApi const& serve,
    std::string const& tree_id,
    std::filesystem::path const& repo_path,
    gsl::not_null<StorageConfig const*> const& native_storage_config,
    StorageConfig const* compat_storage_config,
    Storage const* compat_storage,
    IExecutionApi const* local_api,
    IExecutionApi const* remote_api,
    AsyncMapConsumerLoggerPtr const& logger,
    bool no_sync_is_fatal) -> bool;

#endif  // INCLUDED_SRC_OTHER_TOOLS_ROOT_MAPS_ROOT_UTILS_HPP
