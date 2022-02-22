#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_ACTION_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_ACTION_HPP

#include <chrono>
#include <memory>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/execution_api/common/execution_response.hpp"

class Logger;
class ExecutionArtifactContainer;

/// \brief Abstract action.
/// Can execute multiple commands. Commands are executed in arbitrary order and
/// cannot depend on each other.
class IExecutionAction {
  public:
    using Ptr = std::unique_ptr<IExecutionAction>;

    enum class CacheFlag {
        CacheOutput,       ///< run and cache, or serve from cache
        DoNotCacheOutput,  ///< run and do not cache, never served from cached
        FromCacheOnly,     ///< do not run, only serve from cache
        PretendCached      ///< always run, respond same action id as if cached
    };

    static constexpr std::chrono::milliseconds kDefaultTimeout{1000};

    [[nodiscard]] static constexpr auto CacheEnabled(CacheFlag f) -> bool {
        return f == CacheFlag::CacheOutput or f == CacheFlag::FromCacheOnly;
    }

    [[nodiscard]] static constexpr auto ExecutionEnabled(CacheFlag f) -> bool {
        return f == CacheFlag::CacheOutput or
               f == CacheFlag::DoNotCacheOutput or
               f == CacheFlag::PretendCached;
    }

    IExecutionAction() = default;
    IExecutionAction(IExecutionAction const&) = delete;
    IExecutionAction(IExecutionAction&&) = delete;
    auto operator=(IExecutionAction const&) -> IExecutionAction& = delete;
    auto operator=(IExecutionAction &&) -> IExecutionAction& = delete;
    virtual ~IExecutionAction() = default;

    /// \brief Execute the action.
    /// \returns Execution response, with commands' outputs and artifacts.
    /// \returns nullptr if execution failed.
    // NOLINTNEXTLINE(google-default-arguments)
    [[nodiscard]] virtual auto Execute(Logger const* logger = nullptr) noexcept
        -> IExecutionResponse::Ptr = 0;

    virtual void SetCacheFlag(CacheFlag flag) noexcept = 0;

    virtual void SetTimeout(std::chrono::milliseconds timeout) noexcept = 0;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_ACTION_HPP
