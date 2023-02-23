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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP

#include <functional>
#include <istream>
#include <memory>
#include <string>

#include "src/buildtool/logging/log_level.hpp"

// forward declaration
class Logger;

class ILogSink {
  public:
    using Ptr = std::shared_ptr<ILogSink>;
    ILogSink() noexcept = default;
    ILogSink(ILogSink const&) = delete;
    ILogSink(ILogSink&&) = delete;
    auto operator=(ILogSink const&) -> ILogSink& = delete;
    auto operator=(ILogSink&&) -> ILogSink& = delete;
    virtual ~ILogSink() noexcept = default;

    /// \brief Thread-safe emitting of log messages.
    /// Logger might be 'nullptr' if called from the global context.
    virtual void Emit(Logger const* logger,
                      LogLevel level,
                      std::string const& msg) const noexcept = 0;

  protected:
    /// \brief Helper class for line iteration with std::istream_iterator.
    class Line : public std::string {
        friend auto operator>>(std::istream& is, Line& line) -> std::istream& {
            return std::getline(is, line);
        }
    };
};

using LogSinkFactory = std::function<ILogSink::Ptr()>;

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
