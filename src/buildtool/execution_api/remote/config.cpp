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

#include "src/buildtool/execution_api/remote/config.hpp"

#include <exception>
#include <fstream>

#include "nlohmann/json.hpp"

[[nodiscard]] auto RemoteExecutionConfig::SetRemoteExecutionDispatch(
    const std::filesystem::path& filename) noexcept -> bool {
    nlohmann::json dispatch{};
    try {
        std::ifstream fs(filename);
        dispatch = nlohmann::json::parse(fs);
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failed to read json file {}: {}",
                    filename.string(),
                    e.what());
        return false;
    }
    std::vector<std::pair<std::map<std::string, std::string>, ServerAddress>>
        parsed{};
    try {
        if (not dispatch.is_array()) {
            Logger::Log(LogLevel::Warning,
                        "Endpoint configuration has to be a list of pairs, but "
                        "found {}",
                        dispatch.dump());
            return false;
        }
        for (auto const& entry : dispatch) {
            if (not(entry.is_array() and entry.size() == 2)) {
                Logger::Log(
                    LogLevel::Warning,
                    "Endpoint configuration has to be a list of pairs, but "
                    "found entry {}",
                    entry.dump());
                return false;
            }
            if (not entry[0].is_object()) {
                Logger::Log(LogLevel::Warning,
                            "Property condition has to be given as an object, "
                            "but found {}",
                            entry[0].dump());
                return false;
            }
            std::map<std::string, std::string> props{};
            for (auto const& [k, v] : entry[0].items()) {
                if (not v.is_string()) {
                    Logger::Log(LogLevel::Warning,
                                "Property condition has to be given as an "
                                "object of strings but found {}",
                                entry[0].dump());
                    return false;
                }
                props.emplace(k, v.template get<std::string>());
            }
            if (not entry[1].is_string()) {
                Logger::Log(
                    LogLevel::Warning,
                    "Endpoint has to be specified as string (in the form "
                    "host:port), but found {}",
                    entry[1].dump());
                return false;
            }
            auto endpoint = ParseAddress(entry[1].template get<std::string>());
            if (not endpoint) {
                Logger::Log(LogLevel::Warning,
                            "Failed to parse {} as endpoint.",
                            entry[1].dump());
                return false;
            }
            parsed.emplace_back(props, *endpoint);
        }
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failure analysing endpoint configuration {}: {}",
                    dispatch.dump(),
                    e.what());
        return false;
    }

    try {
        Instance().dispatch_ = parsed;
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Warning,
                    "Failure assigning the endpoint configuration: {}",
                    e.what());
        return false;
    }
    return true;
}
