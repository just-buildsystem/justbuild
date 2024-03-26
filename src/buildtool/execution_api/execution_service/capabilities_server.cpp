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

#include "src/buildtool/execution_api/execution_service/capabilities_server.hpp"

#include <cstddef>

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

auto CapabilitiesServiceImpl::GetCapabilities(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::GetCapabilitiesRequest*
    /*request*/,
    ::bazel_re::ServerCapabilities* response) -> ::grpc::Status {
    if (!Compatibility::IsCompatible()) {
        auto const* str = "GetCapabilities not implemented";
        Logger::Log(LogLevel::Error, str);
        return ::grpc::Status{grpc::StatusCode::UNIMPLEMENTED, str};
    }
    ::bazel_re::CacheCapabilities cache;
    ::bazel_re::ExecutionCapabilities exec;

    cache.add_digest_functions(
        ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA256);
    cache.mutable_action_cache_update_capabilities()->set_update_enabled(false);
    static constexpr std::size_t kMaxBatchTransferSize = 1024 * 1024;
    cache.set_max_batch_total_size_bytes(kMaxBatchTransferSize);
    static_assert(kMaxBatchTransferSize < GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH,
                  "Max batch transfer size too large.");
    cache.add_supported_chunking_algorithms(
        ::bazel_re::ChunkingAlgorithm_Value::
            ChunkingAlgorithm_Value_FASTCDC_MT0_8KB);
    *(response->mutable_cache_capabilities()) = cache;

    exec.set_digest_function(
        ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA256);
    exec.set_exec_enabled(true);

    *(response->mutable_execution_capabilities()) = exec;
    ::build::bazel::semver::SemVer v{};
    v.set_major(2);
    v.set_minor(0);

    *(response->mutable_low_api_version()) = v;
    *(response->mutable_high_api_version()) = v;
    return ::grpc::Status::OK;
}
