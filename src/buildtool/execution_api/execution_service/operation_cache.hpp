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

#ifndef OPERATION_CACHE_HPP
#define OPERATION_CACHE_HPP

#include <atomic>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "google/longrunning/operations.pb.h"
#include "google/protobuf/timestamp.pb.h"

class OperationCache {
    using Operation = ::google::longrunning::Operation;

  public:
    [[nodiscard]] static auto Instance() -> OperationCache& {
        static OperationCache x;
        return x;
    }
    OperationCache() noexcept = default;
    ~OperationCache() noexcept = default;

    OperationCache(OperationCache const&) = delete;
    auto operator=(OperationCache const&) -> OperationCache& = delete;
    OperationCache(OperationCache&&) = delete;
    auto operator=(OperationCache&&) -> OperationCache& = delete;

    static void Set(std::string const& action, Operation const& op) {
        Instance().SetInternal(action, op);
    }

    [[nodiscard]] static auto Query(std::string const& x) noexcept
        -> std::optional<Operation> {
        return Instance().QueryInternal(x);
    }

    static void SetExponent(uint8_t x) noexcept {
        Instance().threshold_ = 1U << x;
    }

  private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ::google::longrunning::Operation> cache_;
    static constexpr uint8_t kDefaultExponent{14};
    std::size_t threshold_{1U << kDefaultExponent};

    void SetInternal(std::string const& action, Operation const& op) {
        GarbageCollection();
        std::unique_lock lock{mutex_};
        cache_[action] = op;
    }

    [[nodiscard]] auto QueryInternal(std::string const& x) const noexcept
        -> std::optional<Operation> {
        std::shared_lock lock{mutex_};
        auto it = cache_.find(x);
        if (it != cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void DropInternal(std::string const& x) noexcept {
        cache_[x].Clear();
        cache_.erase(x);
    }

    void GarbageCollection();
};

#endif
