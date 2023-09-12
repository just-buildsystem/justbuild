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

#ifndef INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP
#define INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP

#include <memory>
#include <optional>
#include <string>

#include "src/buildtool/common/remote/port.hpp"
#include "src/buildtool/serve_api/remote/serve_target_level_cache_client.hpp"

class ServeApi final {
  public:
    using Ptr = std::unique_ptr<ServeApi>;

    ServeApi(std::string const& host, Port port) noexcept;

    ServeApi(ServeApi const&) = delete;
    ServeApi(ServeApi&& other) noexcept;

    auto operator=(ServeApi const&) -> ServeApi& = delete;
    auto operator=(ServeApi&&) -> ServeApi& = delete;

    ~ServeApi();

    [[nodiscard]] auto RetrieveTreeFromCommit(std::string const& commit,
                                              std::string const& subdir = ".",
                                              bool sync_tree = false)
        -> std::optional<std::string>;

  private:
    // target-level cache service client
    std::unique_ptr<ServeTargetLevelCacheClient> tlc_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_SERVE_API_REMOTE_SERVE_API_HPP
