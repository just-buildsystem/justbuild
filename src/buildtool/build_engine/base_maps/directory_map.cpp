#include "src/buildtool/build_engine/base_maps/directory_map.hpp"

#include <filesystem>
#include <unordered_set>

#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/utils/cpp/path.hpp"

auto BuildMaps::Base::CreateDirectoryEntriesMap(std::size_t jobs)
    -> DirectoryEntriesMap {
    auto directory_reader = [](auto /* unused*/,
                               auto setter,
                               auto logger,
                               auto /* unused */,
                               auto const& key) {
        auto const* ws_root =
            RepositoryConfig::Instance().WorkspaceRoot(key.repository);
        if (ws_root == nullptr) {
            (*logger)(
                fmt::format("Cannot determine workspace root for repository {}",
                            key.repository),
                true);
            return;
        }
        auto dir_path = key.module.empty() ? "." : key.module;
        if (not ws_root->IsDirectory(dir_path)) {
            // Missing directory is fine (source tree might be incomplete),
            // contains no entries.
            (*setter)(FileRoot::DirectoryEntries{
                FileRoot::DirectoryEntries::pairs_t{}});
            return;
        }
        (*setter)(ws_root->ReadDirectory(dir_path));
    };
    return AsyncMapConsumer<BuildMaps::Base::ModuleName,
                            FileRoot::DirectoryEntries>{directory_reader, jobs};
}
