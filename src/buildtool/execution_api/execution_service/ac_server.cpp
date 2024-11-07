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

#include "src/buildtool/execution_api/execution_service/ac_server.hpp"

#include <optional>
#include <string>
#include <utility>

#include "fmt/core.h"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/utils/cpp/expected.hpp"

auto ActionCacheServiceImpl::GetActionResult(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::GetActionResultRequest* request,
    ::bazel_re::ActionResult* response) -> ::grpc::Status {
    auto action_digest = ArtifactDigestFactory::FromBazel(
        storage_config_.hash_function.GetType(), request->action_digest());
    if (not action_digest) {
        logger_.Emit(LogLevel::Debug, "{}", action_digest.error());
        return ::grpc::Status{::grpc::StatusCode::INVALID_ARGUMENT,
                              std::move(action_digest).error()};
    }
    logger_.Emit(LogLevel::Trace, "GetActionResult: {}", action_digest->hash());
    auto const lock = GarbageCollector::SharedLock(storage_config_);
    if (not lock) {
        static constexpr auto kStr = "Could not acquire SharedLock";
        logger_.Emit(LogLevel::Error, kStr);
        return grpc::Status{grpc::StatusCode::INTERNAL, kStr};
    }

    auto action_result = storage_.ActionCache().CachedResult(*action_digest);
    if (not action_result) {
        return grpc::Status{
            grpc::StatusCode::NOT_FOUND,
            fmt::format("{} missing from AC", action_digest->hash())};
    }
    *response = *std::move(action_result);
    return ::grpc::Status::OK;
}

auto ActionCacheServiceImpl::UpdateActionResult(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::UpdateActionResultRequest*
    /*request*/,
    ::bazel_re::ActionResult* /*response*/) -> ::grpc::Status {
    static auto constexpr kStr = "UpdateActionResult not implemented";
    logger_.Emit(LogLevel::Error, kStr);
    return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, kStr};
}
