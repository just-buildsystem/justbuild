#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_LOCAL_TREE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_LOCAL_TREE_MAP_HPP

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief Maps digest of `bazel_re::Directory` to `LocalTree`.
class LocalTreeMap {
    /// \brief Thread-safe pool of unique object infos.
    class ObjectInfoPool {
      public:
        /// Get pointer to stored info, or a add new one and return its pointer.
        [[nodiscard]] auto GetOrAdd(Artifact::ObjectInfo const& info)
            -> Artifact::ObjectInfo const* {
            {  // get
                std::shared_lock lock{mutex_};
                auto it = infos_.find(info);
                if (it != infos_.end()) {
                    return &(*it);
                }
            }
            {  // or add
                std::unique_lock lock{mutex_};
                return &(*infos_.emplace(info).first);
            }
        }

      private:
        std::unordered_set<Artifact::ObjectInfo> infos_;
        mutable std::shared_mutex mutex_;
    };

  public:
    /// \brief Maps blob locations to object infos.
    class LocalTree {
        friend class LocalTreeMap;

      public:
        /// \brief Add a new path and info pair to the tree.
        /// Path must not be absolute, empty, or contain dot-segments.
        /// \param path The location to add the object info.
        /// \param info The object info to add.
        /// \returns true if successfully inserted or info existed before.
        [[nodiscard]] auto AddInfo(std::filesystem::path const& path,
                                   Artifact::ObjectInfo const& info) noexcept
            -> bool {
            auto norm_path = path.lexically_normal();
            if (norm_path.is_absolute() or norm_path.empty() or
                *norm_path.begin() == "..") {
                Logger::Log(LogLevel::Error,
                            "cannot add malformed path to local tree: {}",
                            path.string());
                return false;
            }
            try {
                if (entries_.contains(norm_path.string())) {
                    return true;
                }
                if (auto const* info_ptr = infos_->GetOrAdd(info)) {
                    entries_.emplace(norm_path.string(), info_ptr);
                    return true;
                }
            } catch (std::exception const& ex) {
                Logger::Log(LogLevel::Error,
                            "adding object info to tree failed with:\n{}",
                            ex.what());
            }
            return false;
        }

        [[nodiscard]] auto size() const noexcept { return entries_.size(); }
        [[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
        [[nodiscard]] auto end() const noexcept { return entries_.end(); }

      private:
        gsl::not_null<ObjectInfoPool*> infos_;
        std::unordered_map<std::string,
                           gsl::not_null<Artifact::ObjectInfo const*>>
            entries_{};

        explicit LocalTree(gsl::not_null<ObjectInfoPool*> infos) noexcept
            : infos_{std::move(infos)} {}
    };

    /// \brief Create a new `LocalTree` object.
    [[nodiscard]] auto CreateTree() noexcept -> LocalTree {
        return LocalTree{&infos_};
    }

    /// \brief Get pointer to existing `LocalTree` object.
    /// \param root_digest  The root digest of the tree to lookup.
    /// \returns nullptr if no tree was found for given root digest.
    [[nodiscard]] auto GetTree(bazel_re::Digest const& root_digest)
        const noexcept -> LocalTree const* {
        std::shared_lock lock{mutex_};
        auto it = trees_.find(root_digest);
        return (it != trees_.end()) ? &(it->second) : nullptr;
    }

    /// \brief Checks if entry for root digest exists.
    [[nodiscard]] auto HasTree(
        bazel_re::Digest const& root_digest) const noexcept -> bool {
        return GetTree(root_digest) != nullptr;
    }

    /// \brief Add new `LocalTree` for given root digest. Does not overwrite if
    /// a tree for the given root digest already exists.
    /// \param root_digest  The root digest to add the new tree for.
    /// \param tree         The new tree to add.
    /// \returns true if the tree was successfully added or existed before.
    [[nodiscard]] auto AddTree(bazel_re::Digest const& root_digest,
                               LocalTree&& tree) noexcept -> bool {
        if (not HasTree(root_digest)) {
            try {
                std::unique_lock lock{mutex_};
                trees_.emplace(root_digest, std::move(tree));
            } catch (std::exception const& ex) {
                Logger::Log(LogLevel::Error,
                            "adding local tree to tree map failed with:\n{}",
                            ex.what());
                return false;
            }
        }
        return true;
    }

  private:
    ObjectInfoPool infos_;  // pool to store each solid object info exactly once
    std::unordered_map<bazel_re::Digest, LocalTree> trees_;
    mutable std::shared_mutex mutex_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_LOCAL_TREE_MAP_HPP
