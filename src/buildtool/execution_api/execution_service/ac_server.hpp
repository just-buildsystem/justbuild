// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef AC_SERVER_HPP
#define AC_SERVER_HPP

#include "build/bazel/remote/execution/v2/remote_execution.grpc.pb.h"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/local/local_ac.hpp"
#include "src/buildtool/logging/logger.hpp"

class ActionCacheServiceImpl final : public bazel_re::ActionCache::Service {
  public:
    // Retrieve a cached execution result.
    //
    // Implementations SHOULD ensure that any blobs referenced from the
    // [ContentAddressableStorage][build.bazel.remote.execution.v2.ContentAddressableStorage]
    // are available at the time of returning the
    // [ActionResult][build.bazel.remote.execution.v2.ActionResult] and will be
    // for some period of time afterwards. The TTLs of the referenced blobs
    // SHOULD be increased if necessary and applicable.
    //
    // Errors:
    //
    // * `NOT_FOUND`: The requested `ActionResult` is not in the cache.
    auto GetActionResult(::grpc::ServerContext* context,
                         const ::bazel_re::GetActionResultRequest* request,
                         ::bazel_re::ActionResult* response)
        -> ::grpc::Status override;
    // Upload a new execution result.
    //
    // In order to allow the server to perform access control based on the type
    // of action, and to assist with client debugging, the client MUST first
    // upload the [Action][build.bazel.remote.execution.v2.Execution] that
    // produced the result, along with its
    // [Command][build.bazel.remote.execution.v2.Command], into the
    // `ContentAddressableStorage`.
    //
    // Errors:
    //
    // * `INVALID_ARGUMENT`: One or more arguments are invalid.
    // * `FAILED_PRECONDITION`: One or more errors occurred in updating the
    //   action result, such as a missing command or action.
    // * `RESOURCE_EXHAUSTED`: There is insufficient storage space to add the
    //   entry to the cache.
    auto UpdateActionResult(
        ::grpc::ServerContext* context,
        const ::bazel_re::UpdateActionResultRequest* request,
        ::bazel_re::ActionResult* response) -> ::grpc::Status override;

  private:
    LocalCAS<ObjectType::File> cas_{};
    LocalAC ac_{&cas_};
    Logger logger_{"execution-service"};
};

#endif
