#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP

#include <cstdint>
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
#include "src/utils/cpp/type_safe_arithmetic.hpp"

// Port
struct PortTag : type_safe_arithmetic_tag<std::uint16_t> {};
using Port = type_safe_arithmetic<PortTag>;

class RemoteExecutionConfig {
  public:
    struct ServerAddress {
        std::string host{};
        Port port{};
    };

    // Obtain global instance
    [[nodiscard]] static auto Instance() noexcept -> RemoteExecutionConfig& {
        static RemoteExecutionConfig config;
        return config;
    }

    // Set remote execution and cache address, unsets if parsing `address` fails
    [[nodiscard]] static auto SetRemoteAddress(
        std::string const& address) noexcept -> bool {
        auto& inst = Instance();
        return static_cast<bool>(inst.remote_address_ = inst.cache_address_ =
                                     ParseAddress(address));
    }

    // Set specific cache address, unsets if parsing `address` fails
    [[nodiscard]] static auto SetCacheAddress(
        std::string const& address) noexcept -> bool {
        return static_cast<bool>(Instance().cache_address_ =
                                     ParseAddress(address));
    }

    // Add platform property from string of form "key:val"
    [[nodiscard]] static auto AddPlatformProperty(
        std::string const& property) noexcept -> bool {
        if (auto pair = ParseProperty(property)) {
            Instance().platform_properties_[std::move(pair->first)] =
                std::move(pair->second);
            return true;
        }
        return false;
    }

    // Remote execution address, if set
    [[nodiscard]] static auto RemoteAddress() noexcept
        -> std::optional<ServerAddress> {
        return Instance().remote_address_;
    }

    // Cache address, if set
    [[nodiscard]] static auto CacheAddress() noexcept
        -> std::optional<ServerAddress> {
        return Instance().cache_address_;
    }

    [[nodiscard]] static auto PlatformProperties() noexcept
        -> std::map<std::string, std::string> {
        return Instance().platform_properties_;
    }

  private:
    // Server address of remote execution.
    std::optional<ServerAddress> remote_address_{};

    // Server address of cache endpoint for rebuild.
    std::optional<ServerAddress> cache_address_{};

    // Platform properies for execution.
    std::map<std::string, std::string> platform_properties_{};

    [[nodiscard]] static auto ParseAddress(std::string const& address) noexcept
        -> std::optional<ServerAddress> {
        std::istringstream iss(address);
        std::string host;
        std::string port;
        if (not std::getline(iss, host, ':') or
            not std::getline(iss, port, ':')) {
            return std::nullopt;
        }
        try {
            static constexpr int kMaxPortNumber{
                std::numeric_limits<uint16_t>::max()};
            auto port_num = std::stoi(port);
            if (not host.empty() and port_num >= 0 and
                port_num <= kMaxPortNumber) {
                return ServerAddress{std::move(host),
                                     gsl::narrow_cast<Port::value_t>(port_num)};
            }
        } catch (std::out_of_range const& e) {
            Logger::Log(LogLevel::Error, "Port raised out_of_range exception.");
        } catch (std::invalid_argument const& e) {
            Logger::Log(LogLevel::Error,
                        "Port raised invalid_argument exception.");
        }
        return std::nullopt;
    }

    [[nodiscard]] static auto ParseProperty(
        std::string const& property) noexcept
        -> std::optional<std::pair<std::string, std::string>> {
        std::istringstream pss(property);
        std::string key;
        std::string val;
        if (not std::getline(pss, key, ':') or
            not std::getline(pss, val, ':')) {
            return std::nullopt;
        }
        return std::make_pair(key, val);
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_REMOTE_CONFIG_HPP
