#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP

#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/logging/logger.hpp"

class RemoteExecutionConfig {
  public:
    [[nodiscard]] static auto ParseAddress(std::string const& address) noexcept
        -> std::optional<std::pair<std::string, int>> {
        std::istringstream iss(address);
        std::string host;
        std::string port;
        if (not std::getline(iss, host, ':') or
            not std::getline(iss, port, ':')) {
            return std::nullopt;
        }
        try {
            return std::make_pair(host, std::stoi(port));
        } catch (std::out_of_range const& e) {
            Logger::Log(LogLevel::Error, "Port raised out_of_range exception.");
            return std::nullopt;
        } catch (std::invalid_argument const& e) {
            Logger::Log(LogLevel::Error,
                        "Port raised invalid_argument exception.");
            return std::nullopt;
        }
    }

    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteExecutionConfig& {
        static RemoteExecutionConfig config;
        return config;
    }

    [[nodiscard]] auto IsValidAddress() const noexcept -> bool {
        return valid_;
    }

    [[nodiscard]] auto SetAddress(std::string const& address) noexcept -> bool {
        auto pair = ParseAddress(address);
        return pair and SetAddress(pair->first, pair->second);
    }

    [[nodiscard]] auto SetAddress(std::string const& host, int port) noexcept
        -> bool {
        host_ = host;
        port_ = port,
        valid_ = (not host.empty() and port >= 0 and port <= kMaxPortNumber);
        return valid_;
    }

    [[nodiscard]] auto Host() const noexcept -> std::string { return host_; }
    [[nodiscard]] auto Port() const noexcept -> int { return port_; }

  private:
    static constexpr int kMaxPortNumber{std::numeric_limits<uint16_t>::max()};
    std::string host_{};
    int port_{};
    bool valid_{false};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
