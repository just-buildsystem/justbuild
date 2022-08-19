#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP

#include <unordered_map>

#include <nlohmann/json.hpp>

#include "src/buildtool/build_engine/analysed_target/analysed_target.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/common/artifact.hpp"
#ifndef BOOTSTRAP_BUILD_TOOL
#include "src/buildtool/execution_api/common/execution_api.hpp"
#endif
#include "src/buildtool/execution_api/local/file_storage.hpp"
#include "src/buildtool/execution_api/local/local_cas.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

class TargetCache {
  public:
    // Key for target cache. Created from target name and effective config.
    class Key {
      public:
        [[nodiscard]] static auto Create(
            BuildMaps::Base::EntityName const& target,
            Configuration const& effective_config) noexcept
            -> std::optional<Key>;

        [[nodiscard]] auto Id() const& -> Artifact::ObjectInfo const& {
            return id_;
        }
        [[nodiscard]] auto Id() && -> Artifact::ObjectInfo {
            return std::move(id_);
        }
        [[nodiscard]] auto operator==(Key const& other) const -> bool {
            return this->Id() == other.Id();
        }

      private:
        explicit Key(Artifact::ObjectInfo id) : id_{std::move(id)} {}
        Artifact::ObjectInfo id_;
    };

    // Entry for target cache. Created from target, contains TargetResult.
    class Entry {
        friend class TargetCache;

      public:
        // Create the entry from target with replacement artifacts/infos.
        // Replacement artifacts must replace all non-known artifacts by known.
        [[nodiscard]] static auto FromTarget(
            AnalysedTargetPtr const& target,
            std::unordered_map<ArtifactDescription, Artifact::ObjectInfo> const&
                replacements) noexcept -> std::optional<Entry>;

        // Obtain TargetResult from cache entry.
        [[nodiscard]] auto ToResult() const -> std::optional<TargetResult>;

      private:
        nlohmann::json desc_;

        explicit Entry(nlohmann::json desc) : desc_{std::move(desc)} {}
        [[nodiscard]] auto ToJson() const& -> nlohmann::json const& {
            return desc_;
        }
        [[nodiscard]] auto ToJson() && -> nlohmann::json {
            return std::move(desc_);
        }
    };

    TargetCache() = default;
    TargetCache(TargetCache const&) = delete;
    TargetCache(TargetCache&&) = delete;
    auto operator=(TargetCache const&) -> TargetCache& = delete;
    auto operator=(TargetCache&&) -> TargetCache& = delete;
    ~TargetCache() noexcept = default;

    [[nodiscard]] static auto Instance() -> TargetCache& {
        static TargetCache instance;
        return instance;
    }

    // Store new key entry pair in the target cache.
    [[nodiscard]] auto Store(Key const& key, Entry const& value) const noexcept
        -> bool;

    // Read existing entry and object info for given key from the target cache.
    [[nodiscard]] auto Read(Key const& key) const noexcept
        -> std::optional<std::pair<Entry, Artifact::ObjectInfo>>;

#ifndef BOOTSTRAP_BUILD_TOOL
    auto SetLocalApi(gsl::not_null<IExecutionApi*> const& api) noexcept
        -> void {
        local_api_ = api;
    };

    auto SetRemoteApi(gsl::not_null<IExecutionApi*> const& api) noexcept
        -> void {
        remote_api_ = api;
    };
#endif

  private:
    Logger logger_{"TargetCache"};
    FileStorage<ObjectType::File,
                StoreMode::LastWins,
                /*kSetEpochTime=*/false>
        file_store_{ComputeCacheDir()};
#ifndef BOOTSTRAP_BUILD_TOOL
    IExecutionApi* local_api_{};
    IExecutionApi* remote_api_{};
#endif

    [[nodiscard]] auto DownloadKnownArtifacts(Entry const& value) const noexcept
        -> bool;
    [[nodiscard]] auto DownloadKnownArtifactsFromMap(
        Expression::map_t const& expr_map) const noexcept -> bool;
    [[nodiscard]] static auto CAS() noexcept -> LocalCAS<ObjectType::File>& {
        return LocalCAS<ObjectType::File>::Instance();
    }
    [[nodiscard]] static auto ComputeCacheDir() -> std::filesystem::path;
    [[nodiscard]] static auto ExecutionBackendId() -> std::string;
};

namespace std {
template <>
struct hash<TargetCache::Key> {
    [[nodiscard]] auto operator()(TargetCache::Key const& k) const {
        return std::hash<Artifact::ObjectInfo>{}(k.Id());
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_TARGET_MAP_TARGET_CACHE_HPP
