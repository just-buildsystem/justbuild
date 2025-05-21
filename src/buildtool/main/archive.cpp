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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/main/archive.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/artifact_digest_factory.hpp"
#include "src/buildtool/file_system/git_repo.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/expected.hpp"
#include "src/utils/cpp/hex_string.hpp"

extern "C" {
#include <archive.h>
#include <archive_entry.h>
}

namespace {

void archive_write_closer(archive* a) {
    if (a != nullptr) {
        archive_write_close(a);
        archive_write_free(a);
    }
}

void archive_entry_cleanup(archive_entry* entry) {
    if (entry != nullptr) {
        archive_entry_free(entry);
    }
}

auto add_to_archive(archive* archive,
                    IExecutionApi const& api,
                    const Artifact::ObjectInfo& artifact,
                    const std::filesystem::path& location) -> bool {
    auto constexpr kExecutablePerm = 0555;
    auto constexpr kFilePerm = 0444;
    auto constexpr kDefaultPerm = 07777;

    auto payload = api.RetrieveToMemory(artifact);
    if (not payload) {
        Logger::Log(LogLevel::Error,
                    "Failed to retrieve artifact {}",
                    artifact.ToString());
        return false;
    }
    switch (artifact.type) {
        case ObjectType::File:
        case ObjectType::Executable: {
            std::unique_ptr<archive_entry, decltype(&archive_entry_cleanup)>
                entry{archive_entry_new(), archive_entry_cleanup};
            archive_entry_set_pathname(entry.get(), location.string().c_str());
            archive_entry_set_size(entry.get(),
                                   static_cast<la_int64_t>(payload->size()));
            archive_entry_set_filetype(entry.get(), AE_IFREG);
            archive_entry_set_perm(entry.get(),
                                   artifact.type == ObjectType::Executable
                                       ? kExecutablePerm
                                       : kFilePerm);
            archive_write_header(archive, entry.get());
            archive_write_data(archive, payload->c_str(), payload->size());
        } break;
        case ObjectType::Symlink: {
            std::unique_ptr<archive_entry, decltype(&archive_entry_cleanup)>
                entry{archive_entry_new(), archive_entry_cleanup};
            archive_entry_set_pathname(entry.get(), location.string().c_str());
            archive_entry_set_size(entry.get(),
                                   static_cast<la_int64_t>(payload->size()));
            archive_entry_set_filetype(entry.get(), AE_IFLNK);
            archive_entry_set_symlink(entry.get(), payload->c_str());
            archive_entry_set_perm(entry.get(), kDefaultPerm);
            archive_write_header(archive, entry.get());
            archive_write_data(archive, payload->c_str(), payload->size());
        } break;
        case ObjectType::Tree: {
            // avoid creating empty unnamed folder for the initial call
            if (not location.empty()) {
                std::unique_ptr<archive_entry, decltype(&archive_entry_cleanup)>
                    entry{archive_entry_new(), archive_entry_cleanup};
                archive_entry_set_pathname(entry.get(),
                                           location.string().c_str());
                archive_entry_set_size(entry.get(), 0U);
                archive_entry_set_filetype(entry.get(), AE_IFDIR);
                archive_entry_set_perm(entry.get(), kDefaultPerm);
                archive_write_header(archive, entry.get());
            }
            auto git_tree = GitRepo::ReadTreeData(
                *payload,
                artifact.digest.hash(),
                [](auto const& /*unused*/) { return true; },
                /*is_hex_id=*/true);
            if (not git_tree) {
                Logger::Log(LogLevel::Error,
                            "Failed to parse {} as git tree for path {}",
                            artifact.ToString(),
                            location.string());
                return false;
            }
            // Reorder entries to be keyed and sorted by name
            std::map<std::string, Artifact::ObjectInfo> tree{};
            for (auto const& [hash, entries] : *git_tree) {
                auto hex_hash = ToHexString(hash);
                for (auto const& entry : entries) {
                    auto digest =
                        ArtifactDigestFactory::Create(api.GetHashType(),
                                                      hex_hash,
                                                      0,
                                                      IsTreeObject(entry.type));
                    if (not digest) {
                        return false;
                    }
                    tree[entry.name] =
                        Artifact::ObjectInfo{.digest = *std::move(digest),
                                             .type = entry.type,
                                             .failed = false};
                }
            }
            for (auto const& [name, obj] : tree) {
                if (not add_to_archive(archive, api, obj, location / name)) {
                    return false;
                }
            }

        } break;
    }
    return true;
}

}  // namespace

[[nodiscard]] auto GenerateArchive(
    IExecutionApi const& api,
    const Artifact::ObjectInfo& artifact,
    const std::optional<std::filesystem::path>& output_path) -> bool {

    constexpr int kTarBlockSize = 512;

    std::unique_ptr<archive, decltype(&archive_write_closer)> archive{
        archive_write_new(), archive_write_closer};
    if (archive == nullptr) {
        Logger::Log(LogLevel::Error,
                    "Internal error: Call to archive_write_new() failed");
        return false;
    }
    if (archive_write_set_format_pax_restricted(archive.get()) != ARCHIVE_OK) {
        Logger::Log(LogLevel::Error,
                    "Internal error: Call to "
                    "archive_write_set_format_pax_restriced() failed");
        return false;
    }
    if (archive_write_set_bytes_per_block(archive.get(), kTarBlockSize) !=
        ARCHIVE_OK) {
        Logger::Log(LogLevel::Error,
                    "Internal error: Call to "
                    "archive_write_set_bytes_per_block() failed");
        return false;
    }
    if (output_path) {
        if (archive_write_open_filename(
                archive.get(), output_path->string().c_str()) != ARCHIVE_OK) {
            Logger::Log(LogLevel::Error,
                        "Failed to open archive for writing at {}",
                        output_path->string());
            return false;
        }
    }
    else {
        if (archive_write_open_fd(archive.get(), STDOUT_FILENO) != ARCHIVE_OK) {
            Logger::Log(LogLevel::Error,
                        "Failed to open stdout for writing archive to");
            return false;
        }
    }

    if (not add_to_archive(
            archive.get(), api, artifact, std::filesystem::path{""})) {
        return false;
    }
    if (output_path) {
        Logger::Log(LogLevel::Info,
                    "Archive of {} was installed to {}",
                    artifact.ToString(),
                    output_path->string());
    }

    return true;
}

#endif
