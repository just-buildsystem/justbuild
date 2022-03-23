#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/artifact.hpp"

/// \brief Abstract response.
/// Response of an action execution. Contains outputs from multiple commands and
/// a single container with artifacts.
class IExecutionResponse {
  public:
    using Ptr = std::unique_ptr<IExecutionResponse>;
    using ArtifactInfos = std::unordered_map<std::string, Artifact::ObjectInfo>;

    enum class StatusCode { Failed, Success };

    IExecutionResponse() = default;
    IExecutionResponse(IExecutionResponse const&) = delete;
    IExecutionResponse(IExecutionResponse&&) = delete;
    auto operator=(IExecutionResponse const&) -> IExecutionResponse& = delete;
    auto operator=(IExecutionResponse&&) -> IExecutionResponse& = delete;
    virtual ~IExecutionResponse() = default;

    [[nodiscard]] virtual auto Status() const noexcept -> StatusCode = 0;

    [[nodiscard]] virtual auto ExitCode() const noexcept -> int = 0;

    [[nodiscard]] virtual auto IsCached() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto HasStdErr() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto HasStdOut() const noexcept -> bool = 0;

    [[nodiscard]] virtual auto StdErr() noexcept -> std::string = 0;

    [[nodiscard]] virtual auto StdOut() noexcept -> std::string = 0;

    [[nodiscard]] virtual auto ActionDigest() const noexcept -> std::string = 0;

    [[nodiscard]] virtual auto Artifacts() const noexcept -> ArtifactInfos = 0;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_REMOTE_EXECUTION_RESPONSE_HPP
