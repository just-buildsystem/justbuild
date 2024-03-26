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

#include "src/buildtool/execution_api/remote/bazel/bazel_action.hpp"

#include <utility>  // std::move

#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"
#include "src/buildtool/execution_api/utils/outputscheck.hpp"
#include "src/buildtool/logging/log_level.hpp"

BazelAction::BazelAction(
    std::shared_ptr<BazelNetwork> network,
    bazel_re::Digest root_digest,
    std::vector<std::string> command,
    std::vector<std::string> output_files,
    std::vector<std::string> output_dirs,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties) noexcept
    : network_{std::move(network)},
      root_digest_{std::move(root_digest)},
      cmdline_{std::move(command)},
      output_files_{std::move(output_files)},
      output_dirs_{std::move(output_dirs)},
      env_vars_{BazelMsgFactory::CreateMessageVectorFromMap<
          bazel_re::Command_EnvironmentVariable>(env_vars)},
      properties_{BazelMsgFactory::CreateMessageVectorFromMap<
          bazel_re::Platform_Property>(properties)} {
    std::sort(output_files_.begin(), output_files_.end());
    std::sort(output_dirs_.begin(), output_dirs_.end());
}

auto BazelAction::Execute(Logger const* logger) noexcept
    -> IExecutionResponse::Ptr {
    BlobContainer blobs{};
    auto do_cache = CacheEnabled(cache_flag_);
    auto action = CreateBundlesForAction(&blobs, root_digest_, not do_cache);

    if (logger != nullptr) {
        logger->Emit(LogLevel::Trace,
                     "start execution\n"
                     " - exec_dir digest: {}\n"
                     " - action digest: {}",
                     root_digest_.hash(),
                     action.hash());
    }

    if (do_cache) {
        if (auto result =
                network_->GetCachedActionResult(action, output_files_)) {
            if (result->exit_code() == 0 and
                ActionResultContainsExpectedOutputs(
                    *result, output_files_, output_dirs_)

            ) {
                return IExecutionResponse::Ptr{new BazelResponse{
                    action.hash(), network_, {*result, true}}};
            }
        }
    }

    if (ExecutionEnabled(cache_flag_) and network_->UploadBlobs(blobs)) {
        if (auto output = network_->ExecuteBazelActionSync(action)) {
            if (cache_flag_ == CacheFlag::PretendCached) {
                // ensure the same id is created as if caching were enabled
                auto action_id =
                    CreateBundlesForAction(nullptr, root_digest_, false).hash();
                output->cached_result = true;
                return IExecutionResponse::Ptr{new BazelResponse{
                    std::move(action_id), network_, std::move(*output)}};
            }
            return IExecutionResponse::Ptr{
                new BazelResponse{action.hash(), network_, std::move(*output)}};
        }
    }

    return nullptr;
}

auto BazelAction::CreateBundlesForAction(BlobContainer* blobs,
                                         bazel_re::Digest const& exec_dir,
                                         bool do_not_cache) const noexcept
    -> bazel_re::Digest {
    return BazelMsgFactory::CreateActionDigestFromCommandLine(
        cmdline_,
        exec_dir,
        output_files_,
        output_dirs_,
        env_vars_,
        properties_,
        do_not_cache,
        timeout_,
        blobs == nullptr ? std::nullopt
                         : std::make_optional([&blobs](BazelBlob&& blob) {
                               blobs->Emplace(std::move(blob));
                           }));
}
