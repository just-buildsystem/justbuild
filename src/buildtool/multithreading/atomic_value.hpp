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

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP

#include <atomic>
#include <functional>
#include <memory>

#include "src/utils/cpp/atomic.hpp"

// Value that can be set and get atomically. Reset is not thread-safe.
template <class T>
class AtomicValue {
  public:
    AtomicValue() noexcept = default;
    AtomicValue(AtomicValue const& other) noexcept = delete;
    AtomicValue(AtomicValue&& other) noexcept : data_{other.data_.load()} {}
    ~AtomicValue() noexcept = default;

    auto operator=(AtomicValue const& other) noexcept = delete;
    auto operator=(AtomicValue&& other) noexcept = delete;

    // Atomically set value once and return its reference. If this method is
    // called multiple times concurrently, the setter is called only once. In
    // any case, this method blocks until the value is ready.
    [[nodiscard]] auto SetOnceAndGet(
        std::function<T()> const& setter) const& noexcept -> T const& {
        if (data_.load() == nullptr) {
            if (not load_.exchange(true)) {
                data_.store(std::make_shared<T>(setter()));
                data_.notify_all();
            }
            else {
                data_.wait(nullptr);
            }
        }
        return *data_.load();
    }

    [[nodiscard]] auto SetOnceAndGet(std::function<T()> const& setter) && =
        delete;

    // Reset, not thread-safe!
    void Reset() noexcept {
        load_ = false;
        data_ = nullptr;
    }

  private:
    mutable std::atomic<bool> load_{false};
    mutable atomic_shared_ptr<T> data_{nullptr};
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP
