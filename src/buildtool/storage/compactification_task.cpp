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

#include "src/buildtool/storage/compactification_task.hpp"

#include <array>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>  //std::move
#include <vector>

#include "gsl/gsl"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/multithreading/task_system.hpp"
#include "src/utils/cpp/path_hash.hpp"

namespace {
[[nodiscard]] auto GetObjectTask(CompactificationTask const& task,
                                 ObjectType type) noexcept
    -> CompactificationTask::ObjectTask const&;

[[nodiscard]] auto GetFilterTypes(CompactificationTask const& task)
    -> std::vector<ObjectType>;

using FilterResult = std::optional<std::vector<std::filesystem::path>>;
[[nodiscard]] auto FilterEntries(CompactificationTask const& task,
                                 ObjectType type) noexcept -> FilterResult;
}  // namespace

[[nodiscard]] auto CompactifyConcurrently(
    CompactificationTask const& task) noexcept -> bool {
    std::atomic_bool failed = false;
    std::unordered_map<ObjectType, FilterResult> scan_results;
    {
        TaskSystem ts;
        // Filter entries to create execution tasks:
        try {
            for (auto type : GetFilterTypes(task)) {
                auto tstask =
                    [result = &scan_results[type], &failed, type, &task] {
                        *result = ::FilterEntries(task, type);
                        if (not *result) {
                            failed = true;
                        }
                    };
                ts.QueueTask(std::move(tstask));
            }
        } catch (...) {
            ts.Shutdown();
            return false;
        }
    }

    // Init compactification tasks:
    if (not failed) {
        TaskSystem ts;
        for (auto const& [type, subtasks] : scan_results) {
            auto const& task_callback = GetObjectTask(task, type);
            for (auto const& entry : *subtasks) {
                try {
                    auto tstask = [&failed, &task, &task_callback, &entry] {
                        if (not failed and
                            not std::invoke(task_callback, task, entry)) {
                            failed = true;
                        }
                    };
                    ts.QueueTask(std::move(tstask));
                } catch (...) {
                    ts.Shutdown();
                    return false;
                }
            }
        }
    }
    return not failed;
}

namespace {
[[nodiscard]] auto GetObjectTask(CompactificationTask const& task,
                                 ObjectType type) noexcept
    -> CompactificationTask::ObjectTask const& {
    switch (type) {
        case ObjectType::Symlink:
        case ObjectType::File:
            return task.f_task;
        case ObjectType::Executable:
            return task.x_task;
        case ObjectType::Tree:
            return task.t_task;
    }
    Ensures(false);  // unreachable
}

[[nodiscard]] auto GetFilterTypes(CompactificationTask const& task)
    -> std::vector<ObjectType> {
    static constexpr std::array kObjectTypes{
        ObjectType::File, ObjectType::Tree, ObjectType::Executable};

    // Ensure that types point to unique disk locations.
    // Duplication of roots leads to duplication of tasks.
    std::vector<ObjectType> result;
    std::unordered_set<std::filesystem::path> unique_roots;
    for (ObjectType type : kObjectTypes) {
        auto root = task.cas.StorageRoot(type, task.large);
        if (unique_roots.insert(std::move(root)).second) {
            result.emplace_back(type);
        }
    }
    return result;
}

[[nodiscard]] auto FilterEntries(CompactificationTask const& task,
                                 ObjectType type) noexcept -> FilterResult {
    std::vector<std::filesystem::path> result;
    auto const& storage_root = task.cas.StorageRoot(type, task.large);
    // Check there are entries to process:
    if (not FileSystemManager::IsDirectory(storage_root)) {
        return result;
    }

    FileSystemManager::UseDirEntryFunc callback =
        [&task, &storage_root, &result](std::filesystem::path const& entry,
                                        bool /*unused*/) -> bool {
        // Filter entries:
        try {
            if (std::invoke(task.filter, storage_root / entry)) {
                result.push_back(entry);
            }
        } catch (...) {
            return false;
        }
        return true;
    };

    // Read the ObjectType storage directory:
    if (not FileSystemManager::ReadDirectoryEntriesRecursive(storage_root,
                                                             callback)) {
        result.clear();
        task.Log(LogLevel::Error,
                 "Scanning: Failed to read {}",
                 storage_root.string());
        return std::nullopt;
    }
    return result;
}
}  // namespace
