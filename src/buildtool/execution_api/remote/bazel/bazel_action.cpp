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

#include <algorithm>
#include <compare>
#include <functional>
#include <unordered_map>
#include <utility>  // std::move

#include <grpcpp/support/status.h>

#include "gsl/gsl"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_blob_container.hpp"
#include "src/buildtool/execution_api/bazel_msg/bazel_msg_factory.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_execution_client.hpp"
#include "src/buildtool/execution_api/remote/bazel/bazel_response.hpp"
#include "src/buildtool/execution_api/utils/outputscheck.hpp"
#include "src/buildtool/logging/log_level.hpp"

BazelAction::BazelAction(
    std::shared_ptr<BazelNetwork> network,
    ArtifactDigest root_digest,
    std::vector<std::string> command,
    std::string cwd,
    std::vector<std::string> output_files,
    std::vector<std::string> output_dirs,
    std::map<std::string, std::string> const& env_vars,
    std::map<std::string, std::string> const& properties) noexcept
    : network_{std::move(network)},
      root_digest_{std::move(root_digest)},
      cmdline_{std::move(command)},
      cwd_{std::move(cwd)},
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
    BazelBlobContainer blobs{};
    auto do_cache = CacheEnabled(cache_flag_);
    auto action = CreateBundlesForAction(&blobs, root_digest_, not do_cache);
    if (not action) {
        if (logger != nullptr) {
            logger->Emit(LogLevel::Error,
                         "failed to create an action digest for {}",
                         root_digest_.hash());
        }
        return nullptr;
    }

    if (logger != nullptr) {
        logger->Emit(LogLevel::Trace,
                     "start execution\n"
                     " - exec_dir digest: {}\n"
                     " - action digest: {}",
                     root_digest_.hash(),
                     action->hash());
    }

    auto create_response = [](Logger const* logger,
                              std::string const& action_hash,
                              auto&&... args) -> IExecutionResponse::Ptr {
        try {
            return IExecutionResponse::Ptr{new BazelResponse{
                action_hash, std::forward<decltype(args)>(args)...}};
        } catch (...) {
            if (logger != nullptr) {
                logger->Emit(LogLevel::Error,
                             "failed to create a response for {}",
                             action_hash);
            }
        }
        return nullptr;
    };

    if (do_cache) {
        if (auto result =
                network_->GetCachedActionResult(*action, output_files_)) {
            if (result->exit_code() == 0 and
                ActionResultContainsExpectedOutputs(
                    *result, output_files_, output_dirs_)

            ) {
                return create_response(
                    logger,
                    action->hash(),
                    network_,
                    BazelExecutionClient::ExecutionOutput{*result, true});
            }
        }
    }

    if (ExecutionEnabled(cache_flag_) and
        network_->UploadBlobs(std::move(blobs))) {
        if (auto output = network_->ExecuteBazelActionSync(*action)) {
            if (cache_flag_ == CacheFlag::PretendCached) {
                // ensure the same id is created as if caching were enabled
                auto action_cached =
                    CreateBundlesForAction(nullptr, root_digest_, false);
                if (not action_cached) {
                    if (logger != nullptr) {
                        logger->Emit(
                            LogLevel::Error,
                            "failed to create a cached action digest for {}",
                            root_digest_.hash());
                    }
                    return nullptr;
                }

                output->cached_result = true;
                return create_response(logger,
                                       action_cached->hash(),
                                       network_,
                                       *std::move(output));
            }
            return create_response(
                logger, action->hash(), network_, *std::move(output));
        }
    }

    return nullptr;
}

auto BazelAction::CreateBundlesForAction(BazelBlobContainer* blobs,
                                         ArtifactDigest const& exec_dir,
                                         bool do_not_cache) const noexcept
    -> std::optional<bazel_re::Digest> {
    using StoreFunc = BazelMsgFactory::ActionDigestRequest::BlobStoreFunc;
    std::optional<StoreFunc> store_blob = std::nullopt;
    if (blobs != nullptr) {
        store_blob = [&blobs](BazelBlob&& blob) {
            blobs->Emplace(std::move(blob));
        };
    }
    BazelMsgFactory::ActionDigestRequest request{
        .command_line = &cmdline_,
        .cwd = &cwd_,
        .output_files = &output_files_,
        .output_dirs = &output_dirs_,
        .env_vars = &env_vars_,
        .properties = &properties_,
        .exec_dir = &exec_dir,
        .hash_function = network_->GetHashFunction(),
        .timeout = timeout_,
        .skip_action_cache = do_not_cache,
        .store_blob = std::move(store_blob)};
    auto const action_digest =
        BazelMsgFactory::CreateActionDigestFromCommandLine(request);
    if (not action_digest) {
        return std::nullopt;
    }
    return ArtifactDigestFactory::ToBazel(*action_digest);
}
