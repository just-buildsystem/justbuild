#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_HPP

#include <functional>
#include <type_traits>

class Task {
  public:
    using TaskFunc = std::function<void()>;

    Task() noexcept = default;

    // NOLINTNEXTLINE(modernize-pass-by-value)
    explicit Task(TaskFunc const& function) noexcept : f_{function} {}
    explicit Task(TaskFunc&& function) noexcept : f_{std::move(function)} {}

    void operator()() { f_(); }

    // To be able to discern whether the internal f_ has been set or not,
    // allowing us to write code such as:
    /*
    Task t;
    while (!t) {
        t = TryGetTaskFromQueue(); // (*)
    }
    t(); // (**)
    */
    // (*) does `return Task();` or `return {};` if queue is empty or locked)
    // (**) we can now surely execute the task (and be sure it won't throw any
    // exception) (for the sake of the example, imagine we are sure that the
    // queue wasn't empty, otherwise this would be an infinite loop)
    explicit operator bool() const noexcept { return f_.operator bool(); }

  private:
    TaskFunc f_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_TASK_HPP
