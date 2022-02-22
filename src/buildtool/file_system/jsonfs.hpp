#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_JSONFS_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_JSONFS_HPP

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>

#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/utils/cpp/json.hpp"

class Json {
  public:
    // Note that we are not using std::pair<std::nlohmann, bool> and being
    // coherent with FileSystemManager::ReadFile because there is a bug in llvm
    // toolchain related to type_traits that does not allow us to use
    // std::pair<T,U> where T or U are nlohmann::json.
    // LLVM bug report: https://bugs.llvm.org/show_bug.cgi?id=48507
    // Minimal example: https://godbolt.org/z/zacedsGzo
    [[nodiscard]] static auto ReadFile(
        std::filesystem::path const& file) noexcept
        -> std::optional<nlohmann::json> {
        auto const type = FileSystemManager::Type(file);
        if (not type or not IsFileObject(*type)) {
            Logger::Log(LogLevel::Debug,
                        "{} can not be read because it is not a file.",
                        file.string());
            return std::nullopt;
        }
        try {
            nlohmann::json content;
            std::ifstream file_reader(file.string());
            if (file_reader.is_open()) {
                file_reader >> content;
                return content;
            }
            return std::nullopt;
        } catch (std::exception const& e) {
            Logger::Log(LogLevel::Error, e.what());
            return std::nullopt;
        }
    }

};  // Class Json

#endif  // INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_JSONFS_HPP
