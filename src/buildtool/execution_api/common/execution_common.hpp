#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_COMMON_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_COMMON_HPP

#ifdef __unix__
#include <sys/types.h>
#include <unistd.h>
#else
#error "Non-unix is not supported yet"
#endif

#include <array>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/crypto/hash_generator.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

/// \brief Create unique ID for current process and thread.
[[nodiscard]] static inline auto CreateProcessUniqueId() noexcept
    -> std::optional<std::string> {
#ifdef __unix__
    pid_t pid{};
    try {
        pid = getpid();
    } catch (std::exception const& e) {
        Logger::Log(LogLevel::Error, e.what());
        return std::nullopt;
    }
#endif
    auto tid = std::this_thread::get_id();
    std::ostringstream id{};
    id << pid << "-" << tid;
    return id.str();
}

/// \brief Create unique path based on file_path.
[[nodiscard]] static inline auto CreateUniquePath(
    std::filesystem::path file_path) noexcept
    -> std::optional<std::filesystem::path> {
    auto id = CreateProcessUniqueId();
    if (id) {
        return file_path.concat("." + *id);
    }
    return std::nullopt;
}

[[nodiscard]] static auto GetNonDeterministicRandomNumber() -> unsigned int {
    std::uniform_int_distribution<unsigned int> dist{};
    std::random_device urandom{
#ifdef __unix__
        "/dev/urandom"
#endif
    };
    return dist(urandom);
}

static auto const kRandomConstant = GetNonDeterministicRandomNumber();

static void EncodeUUIDVersion4(std::string* uuid) {
    constexpr auto kVersionByte = 6UL;
    constexpr auto kVersionBits = 0x40U;  // version 4: 0100 xxxx
    constexpr auto kClearMask = 0x0fU;
    gsl_Expects(uuid->size() >= kVersionByte);
    auto& byte = uuid->at(kVersionByte);
    byte = static_cast<char>(kVersionBits |
                             (kClearMask & static_cast<std::uint8_t>(byte)));
}

static void EncodeUUIDVariant1(std::string* uuid) {
    constexpr auto kVariantByte = 8UL;
    constexpr auto kVariantBits = 0x80U;  // variant 1: 10xx xxxx
    constexpr auto kClearMask = 0x3fU;
    gsl_Expects(uuid->size() >= kVariantByte);
    auto& byte = uuid->at(kVariantByte);
    byte = static_cast<char>(kVariantBits |
                             (kClearMask & static_cast<std::uint8_t>(byte)));
}

/// \brief Create UUID version 4 from seed.
[[nodiscard]] static inline auto CreateUUIDVersion4(std::string const& seed)
    -> std::string {
    constexpr auto kRawLength = 16UL;
    constexpr auto kHexDashPos = std::array{8UL, 12UL, 16UL, 20UL};

    auto value = fmt::format("{}-{}", std::to_string(kRandomConstant), seed);
    auto uuid = HashGenerator::Instance().Run(value).Bytes();
    EncodeUUIDVersion4(&uuid);
    EncodeUUIDVariant1(&uuid);
    gsl_Expects(uuid.size() >= kRawLength);

    std::size_t cur{};
    std::ostringstream ss{};
    auto uuid_hex = ToHexString(uuid.substr(0, kRawLength));
    for (auto pos : kHexDashPos) {
        ss << uuid_hex.substr(cur, pos - cur) << '-';
        cur = pos;
    }
    ss << uuid_hex.substr(cur);
    gsl_EnsuresAudit(ss.str().size() == (2 * kRawLength) + kHexDashPos.size());
    return ss.str();
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_EXECUTION_COMMON_HPP
