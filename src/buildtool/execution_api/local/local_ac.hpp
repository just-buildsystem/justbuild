#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/bazel_types.hpp"
#include "src/buildtool/execution_api/common/execution_common.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

class LocalAC {
  public:
    explicit LocalAC(gsl::not_null<LocalCAS<ObjectType::File>*> cas) noexcept
        : cas_{std::move(cas)} {};

    LocalAC(gsl::not_null<LocalCAS<ObjectType::File>*> cas,
            std::filesystem::path cache_root) noexcept
        : cas_{std::move(cas)}, cache_root_{std::move(cache_root)} {}

    LocalAC(LocalAC const&) = delete;
    LocalAC(LocalAC&&) = delete;
    auto operator=(LocalAC const&) -> LocalAC& = delete;
    auto operator=(LocalAC&&) -> LocalAC& = delete;
    ~LocalAC() noexcept = default;

    [[nodiscard]] auto StoreResult(
        bazel_re::Digest const& action_id,
        bazel_re::ActionResult const& result) const noexcept -> bool {
        auto bytes = result.SerializeAsString();
        auto digest = cas_->StoreBlobFromBytes(bytes);
        return (digest and file_store_.AddFromBytes(
                               action_id.hash(), digest->SerializeAsString()));
    }

    [[nodiscard]] auto CachedResult(bazel_re::Digest const& action_id)
        const noexcept -> std::optional<bazel_re::ActionResult> {
        auto entry_path = file_store_.GetPath(action_id.hash());
        bazel_re::Digest digest{};
        auto const entry =
            FileSystemManager::ReadFile(entry_path, ObjectType::File);
        if (not entry.has_value()) {
            logger_.Emit(LogLevel::Debug,
                         "Cache miss, entry not found {}",
                         entry_path.string());
            return std::nullopt;
        }
        if (not digest.ParseFromString(*entry)) {
            logger_.Emit(LogLevel::Warning,
                         "Parsing cache entry failed failed for action {}",
                         action_id.hash());
            return std::nullopt;
        }
        auto src_path = cas_->BlobPath(digest);
        bazel_re::ActionResult result{};
        if (src_path) {
            auto const bytes = FileSystemManager::ReadFile(*src_path);
            if (bytes.has_value() and result.ParseFromString(*bytes)) {
                return result;
            }
        }
        logger_.Emit(LogLevel::Warning,
                     "Parsing action result failed for action {}",
                     action_id.hash());
        return std::nullopt;
    }

  private:
    // The action cache stores the results of failed actions. For those to be
    // overwritable by subsequent runs we need to choose the store mode "last
    // wins" for the underlying file storage.
    static constexpr auto kStoreMode = StoreMode::LastWins;

    Logger logger_{"LocalAC"};
    gsl::not_null<LocalCAS<ObjectType::File>*> cas_;
    std::filesystem::path const cache_root_{
        LocalExecutionConfig::GetCacheDir()};
    FileStorage<ObjectType::File, kStoreMode, /*kSetEpochTime=*/false>
        file_store_{cache_root_ / "ac"};
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_LOCAL_AC_HPP
