#ifndef INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP
#define INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP

#include <atomic>
#include <condition_variable>
#include <shared_mutex>

// Atomic wrapper with notify/wait capabilities.
// TODO(modernize): Replace any use this class by C++20's std::atomic<T>, once
// libcxx adds support for notify_*() and wait().
// [https://libcxx.llvm.org/docs/Cxx2aStatus.html]
template <class T>
class atomic {
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

    template <class U = T, class = std::enable_if_t<std::is_integral_v<U>>>
    auto operator++() -> T {
        std::shared_lock lock(mutex_);
        return ++value_;
    }
    template <class U = T, class = std::enable_if_t<std::is_integral_v<U>>>
    [[nodiscard]] auto operator++(int) -> T {
        std::shared_lock lock(mutex_);
        return value_++;
    }
    template <class U = T, class = std::enable_if_t<std::is_integral_v<U>>>
    auto operator--() -> T {
        std::shared_lock lock(mutex_);
        return --value_;
    }
    template <class U = T, class = std::enable_if_t<std::is_integral_v<U>>>
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
    mutable std::shared_mutex mutex_{};
    mutable std::condition_variable_any cv_{};
};

// Atomic shared_pointer with notify/wait capabilities.
// TODO(modernize): Replace any use this class by C++20's
// std::atomic<std::shared_ptr<T>>, once libcxx adds support for it.
// [https://libcxx.llvm.org/docs/Cxx2aStatus.html]
template <class T>
class atomic_shared_ptr {
    using ptr_t = std::shared_ptr<T>;

  public:
    atomic_shared_ptr() = default;
    explicit atomic_shared_ptr(ptr_t value) : value_{std::move(value)} {}
    atomic_shared_ptr(atomic_shared_ptr const& other) = delete;
    atomic_shared_ptr(atomic_shared_ptr&& other) = delete;
    ~atomic_shared_ptr() = default;

    auto operator=(atomic_shared_ptr const& other)
        -> atomic_shared_ptr& = delete;
    auto operator=(atomic_shared_ptr&& other) -> atomic_shared_ptr& = delete;
    auto operator=(ptr_t desired) -> ptr_t {  // NOLINT
        std::shared_lock lock(mutex_);
        value_ = desired;
        return desired;
    }
    operator ptr_t() const { value_; }  // NOLINT

    void store(ptr_t desired) {
        std::shared_lock lock(mutex_);
        value_ = std::move(desired);
    }
    [[nodiscard]] auto load() const -> ptr_t { return value_; }

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }
    void wait(ptr_t old) const {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [this, &old]() { return value_ != old; });
    }

  private:
    ptr_t value_{};
    mutable std::shared_mutex mutex_{};
    mutable std::condition_variable_any cv_{};
};

#endif  // INCLUDED_SRC_UTILS_CPP_ATOMIC_HPP
