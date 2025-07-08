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

#include "build/bazel/semver/semver.pb.h"
#include "src/buildtool/common/protocol_traits.hpp"
#include "src/buildtool/execution_api/common/message_limits.hpp"

auto CapabilitiesServiceImpl::GetCapabilities(
    ::grpc::ServerContext* /*context*/,
    const ::bazel_re::GetCapabilitiesRequest*
    /*request*/,
    ::bazel_re::ServerCapabilities* response) -> ::grpc::Status {
    ::bazel_re::CacheCapabilities cache;
    ::bazel_re::ExecutionCapabilities exec;

    cache.add_digest_functions(
        ProtocolTraits::IsNative(hash_type_)
            ? ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA1
            : ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA256);
    cache.mutable_action_cache_update_capabilities()->set_update_enabled(false);
    cache.set_max_batch_total_size_bytes(MessageLimits::kMaxGrpcLength);

    *(response->mutable_cache_capabilities()) = cache;

    exec.set_digest_function(
        ProtocolTraits::IsNative(hash_type_)
            ? ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA1
            : ::bazel_re::DigestFunction_Value::DigestFunction_Value_SHA256);
    exec.set_exec_enabled(true);

    *(response->mutable_execution_capabilities()) = exec;
    ::build::bazel::semver::SemVer low_v{};
    low_v.set_major(2);
    low_v.set_minor(0);
    ::build::bazel::semver::SemVer high_v{};
    high_v.set_major(2);
    high_v.set_minor(1);

    *(response->mutable_low_api_version()) = low_v;
    *(response->mutable_high_api_version()) = high_v;
    return ::grpc::Status::OK;
}
