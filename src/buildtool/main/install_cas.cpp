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

#include "src/buildtool/main/install_cas.hpp"

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/execution_api/utils/subobject.hpp"
#endif
#include "src/buildtool/execution_api/remote/config.hpp"

namespace {

[[nodiscard]] auto InvalidSizeString(std::string const& size_str,
                                     std::string const& hash) noexcept -> bool {
    static auto const kEmptyHash = HashFunction::ComputeBlobHash("");
    return Compatibility::IsCompatible() and          // native mode is fine
           (size_str == "0" or size_str.empty()) and  // not "0" or "" is fine
           kEmptyHash.HexString() != hash and         // empty hash is fine
           RemoteExecutionConfig::RemoteAddress();    // local is fine
}

}  // namespace

[[nodiscard]] auto ObjectInfoFromLiberalString(std::string const& s) noexcept
    -> Artifact::ObjectInfo {
    std::istringstream iss(s);
    std::string id{};
    std::string size_str{};
    std::string type{"f"};
    if (iss.peek() == '[') {
        (void)iss.get();
    }
    std::getline(iss, id, ':');
    if (not iss.eof()) {
        std::getline(iss, size_str, ':');
    }
    if (not iss.eof()) {
        std::getline(iss, type, ']');
    }
    if (InvalidSizeString(size_str, id)) {
        Logger::Log(
            LogLevel::Warning,
            "{} size in object-id is not supported in compatiblity mode.",
            size_str.empty() ? "omitting the" : "zero");
    }
    auto size = static_cast<std::size_t>(
        size_str.empty() ? 0 : std::atol(size_str.c_str()));
    auto const& object_type = FromChar(*type.c_str());
    return Artifact::ObjectInfo{
        .digest = ArtifactDigest{id, size, IsTreeObject(object_type)},
        .type = object_type};
}

#ifndef BOOTSTRAP_BUILD_TOOL
auto FetchAndInstallArtifacts(
    gsl::not_null<IExecutionApi*> const& api,
    gsl::not_null<IExecutionApi*> const& alternative_api,
    FetchArguments const& clargs) -> bool {
    auto object_info = ObjectInfoFromLiberalString(clargs.object_id);

    if (clargs.remember) {
        if (not api->ParallelRetrieveToCas(
                {object_info}, alternative_api, 1, true)) {
            Logger::Log(LogLevel::Warning,
                        "Failed to copy artifact {} to local CAS",
                        object_info.ToString());
        }
    }

    if (clargs.sub_path) {
        std::filesystem::path sofar{};
        auto new_object_info =
            RetrieveSubPathId(object_info, api, *clargs.sub_path);
        if (new_object_info) {
            object_info = *new_object_info;
        }
        else {
            return false;
        }
    }

    if (clargs.output_path) {
        auto output_path = (*clargs.output_path / "").parent_path();
        if (FileSystemManager::IsDirectory(output_path)) {
            output_path /= object_info.digest.hash();
        }

        if (not FileSystemManager::CreateDirectory(output_path.parent_path())) {
            Logger::Log(LogLevel::Error,
                        "failed to create parent directory {}.",
                        output_path.parent_path().string());
            return false;
        }
        if (not api->RetrieveToPaths(
                {object_info}, {output_path}, alternative_api)) {
            Logger::Log(LogLevel::Error, "failed to retrieve artifact.");
            return false;
        }

        Logger::Log(LogLevel::Info,
                    "artifact {} was installed to {}",
                    object_info.ToString(),
                    output_path.string());
    }
    else {  // dump to stdout
        if (not api->RetrieveToFds(
                {object_info}, {dup(fileno(stdout))}, clargs.raw_tree)) {
            Logger::Log(LogLevel::Error, "failed to dump artifact.");
            return false;
        }
    }

    return true;
}
#endif
