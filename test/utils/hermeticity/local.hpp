#ifndef INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP
#define INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP

#include "src/buildtool/common/statistics.hpp"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/logging/logger.hpp"

class HermeticLocalTestFixture {
  public:
    HermeticLocalTestFixture() noexcept {
        static int id{};
        Statistics::Instance().Reset();
        CreateAndSetCleanDiskCache(id++);
    }

  private:
    static void CreateAndSetCleanDiskCache(int case_id) {
        auto test_dir = FileSystemManager::GetCurrentDirectory();
        auto case_dir = test_dir / "tmp" / ("case_" + std::to_string(case_id));

        if (FileSystemManager::RemoveDirectory(case_dir, true) and
            FileSystemManager::CreateDirectoryExclusive(case_dir) and
            LocalExecutionConfig::SetBuildRoot(case_dir)) {
            Logger::Log(LogLevel::Debug,
                        "created test-local cache dir {}",
                        case_dir.string());
        }
        else {
            Logger::Log(LogLevel::Error,
                        "failed to create a test-local cache dir {}",
                        case_dir.string());
            std::exit(EXIT_FAILURE);
        }
    }
};

#endif  // INCLUDED_SRC_TEST_UTILS_HERMETICITY_LOCAL_HPP
