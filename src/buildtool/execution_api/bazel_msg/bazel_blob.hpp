#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"

struct BazelBlob {
    BazelBlob(bazel_re::Digest mydigest, std::string mydata)
        : digest{std::move(mydigest)}, data{std::move(mydata)} {}

    bazel_re::Digest digest{};
    std::string data{};
};

[[nodiscard]] static inline auto CreateBlobFromFile(
    std::filesystem::path const& file_path) noexcept
    -> std::optional<BazelBlob> {
    auto const content = FileSystemManager::ReadFile(file_path);
    if (not content.has_value()) {
        return std::nullopt;
    }
    return BazelBlob{ArtifactDigest::Create(*content), *content};
}

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
