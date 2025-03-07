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

#ifndef INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP
#define INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>  //std::unique_lock
#include <shared_mutex>
#include <type_traits>  // IWYU pragma: keep
#include <utility>      // IWYU pragma: keep

// Atomic wrapper with notify/wait capabilities.
// TODO(modernize): Replace any use this class by C++20's std::atomic<T>, once
// libcxx adds support for notify_*() and wait().
// [https://libcxx.llvm.org/Status/Cxx20.html]
template <class T>
class atomic {  // NOLINT(readability-identifier-naming)
  public:
    atomic() = default;
    explicit atomic(T value) : value_{std::move(value)} {}
    atomic(atomic const& other) = delete;
    atomic(atomic&& other) = delete;
    ~atomic() = default;

    auto operator=(atomic const& other) -> atomic& = delete;
    auto operator=(atomic&& other) -> atomic& = delete;
    auto operator=(T desired) -> T {  // NOLINT
        std::shared_lock lock(mutex_);
        value_ = desired;
        return desired;
    }
    operator T() const { return static_cast<T>(value_); }  // NOLINT

    void store(T desired, std::memory_order order = std::memory_order_seq_cst) {
        std::shared_lock lock(mutex_);
        value_.store(std::move(desired), order);
    }
    [[nodiscard]] auto load(
        std::memory_order order = std::memory_order_seq_cst) const -> T {
        return value_.load(order);
    }

    template <class U = T>
        requires(std::is_integral_v<U>)
    auto operator++() -> T {
        std::shared_lock lock(mutex_);
        return ++value_;
    }
    template <class U = T>
        requires(std::is_integral_v<U>)
    [[nodiscard]] auto operator++(int) -> T {
        std::shared_lock lock(mutex_);
        return value_++;
    }
    template <class U = T>
        requires(std::is_integral_v<U>)
    auto operator--() -> T {
        std::shared_lock lock(mutex_);
        return --value_;
    }
    template <class U = T>
        requires(std::is_integral_v<U>)
    [[nodiscard]] auto operator--(int) -> T {
        std::shared_lock lock(mutex_);
        return value_--;
    }

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }
    void wait(T old,
              std::memory_order order = std::memory_order::seq_cst) const {
        std::unique_lock lock(mutex_);
        cv_.wait(lock,
                 [this, &old, order]() { return value_.load(order) != old; });
    }

  private:
    std::atomic<T> value_{};
    mutable std::shared_mutex mutex_;
    mutable std::condition_variable_any cv_;
};

// Atomic shared_pointer with notify/wait capabilities.
// TODO(modernize): Replace any use this class by C++20's
// std::atomic<std::shared_ptr<T>>, once libcxx adds support for it.
// [https://libcxx.llvm.org/Status/Cxx20.html]
template <class T>
class atomic_shared_ptr {  // NOLINT(readability-identifier-naming)
    using ptr_t = std::shared_ptr<T>;

  public:
    atomic_shared_ptr() = default;
    explicit atomic_shared_ptr(ptr_t value) : value_{std::move(value)} {}
    atomic_shared_ptr(atomic_shared_ptr const& other) = delete;
    atomic_shared_ptr(atomic_shared_ptr&& other) = delete;
    ~atomic_shared_ptr() = default;

    auto operator=(atomic_shared_ptr const& other) -> atomic_shared_ptr& =
                                                          delete;
    auto operator=(atomic_shared_ptr&& other) -> atomic_shared_ptr& = delete;
    auto operator=(ptr_t desired) -> ptr_t {  // NOLINT
        std::shared_lock lock(mutex_);
        std::atomic_store(&value_, desired);
        return desired;
    }
    operator ptr_t() const { return std::atomic_load(&value_); }  // NOLINT

    void store(ptr_t desired) {
        std::shared_lock lock(mutex_);
        std::atomic_store(&value_, std::move(desired));
    }
    [[nodiscard]] auto load() const -> ptr_t {
        return std::atomic_load(&value_);
    }

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }
    void wait(ptr_t old) const {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this, &old]() { return value_ != old; });
    }

  private:
    ptr_t value_{};
    mutable std::shared_mutex mutex_;
    mutable std::condition_variable_any cv_;
};

#endif  // INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP
