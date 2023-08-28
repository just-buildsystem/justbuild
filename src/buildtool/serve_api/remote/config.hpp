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

#include <filesystem>
#include <iterator>
#include <vector>

#include "src/buildtool/execution_api/remote/config.hpp"

class RemoteServeConfig : public RemoteExecutionConfig {
  public:
    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteServeConfig& {
        static RemoteServeConfig config;
        return config;
    }

    // Set the list of known repositories
    [[nodiscard]] static auto SetKnownRepositories(
        std::vector<std::filesystem::path> const& repos) noexcept -> bool {
        auto& inst = Instance();
        inst.repositories_ = std::vector<std::filesystem::path>(
            std::make_move_iterator(repos.begin()),
            std::make_move_iterator(repos.end()));
        return repos.size() == inst.repositories_.size();
    }

    // Repositories known to 'just serve'
    [[nodiscard]] static auto KnownRepositories() noexcept
        -> const std::vector<std::filesystem::path>& {
        return Instance().repositories_;
    }

  private:
    // Known Git repositories to serve server.
    std::vector<std::filesystem::path> repositories_{};
};
