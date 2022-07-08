#include "src/buildtool/common/artifact.hpp"

#include <string>

auto Artifact::ObjectInfo::LiberalFromString(std::string const& s) noexcept
    -> Artifact::ObjectInfo {
    std::istringstream iss(s);
    std::string id{};
    std::string size_str{"0"};
    std::string type{"f"};
    if (iss.peek() == '[') {
        (void)iss.get();
    }
    std::getline(iss, id, ':');
    if (not iss.eof()) {
        std::getline(iss, size_str, ':');
    }
    if (not iss.eof()) {
        std::getline(iss, type, ']');
    }
    auto size = static_cast<std::size_t>(std::atol(size_str.c_str()));
    return Artifact::ObjectInfo{ArtifactDigest{id, size},
                                FromChar(*type.c_str())};
}
