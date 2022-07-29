#include "src/buildtool/main/version.hpp"

#include "nlohmann/json.hpp"
#include "src/utils/cpp/json.hpp"

auto version() -> std::string {
    std::size_t major = 0;
    std::size_t minor = 1;
    std::size_t revision = 1;
    std::string suffix = "+devel";
#ifdef VERSION_EXTRA_SUFFIX
    suffix += VERSION_EXTRA_SUFFIX;
#endif

    nlohmann::json version_info = {{"version", {major, minor, revision}},
                                   {"suffix", suffix}};

#ifdef SOURCE_DATE_EPOCH
    version_info["SOURCE_DATE_EPOCH"] = (std::size_t)SOURCE_DATE_EPOCH;
#else
    version_info["SOURCE_DATE_EPOCH"] = nullptr;
#endif

    return IndentOnlyUntilDepth(version_info, 2, 1, {});
}
