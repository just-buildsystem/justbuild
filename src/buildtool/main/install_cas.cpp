#include "src/buildtool/main/install_cas.hpp"

#include "src/buildtool/compatibility/compatibility.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/execution_api/remote/config.hpp"

auto ObjectInfoFromLiberalString(std::string const& s) noexcept
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
    auto size = static_cast<std::size_t>(
        size_str.empty() ? 0 : std::atol(size_str.c_str()));
    auto const& object_type = FromChar(*type.c_str());
    return Artifact::ObjectInfo{
        ArtifactDigest{id, size, IsTreeObject(object_type)}, object_type};
}

#ifndef BOOTSTRAP_BUILD_TOOL
auto FetchAndInstallArtifacts(gsl::not_null<IExecutionApi*> const& api,
                              FetchArguments const& clargs) -> bool {
    auto object_info = ObjectInfoFromLiberalString(clargs.object_id);

    if (clargs.output_path) {
        auto output_path = (*clargs.output_path / "").parent_path();
        if (FileSystemManager::IsDirectory(output_path)) {
            output_path /= object_info.digest.hash();
        }

        if (not FileSystemManager::CreateDirectory(output_path.parent_path()) or
            not api->RetrieveToPaths({object_info}, {output_path})) {
            Logger::Log(LogLevel::Error, "failed to retrieve artifact.");
            return false;
        }

        Logger::Log(LogLevel::Info,
                    "artifact {} was installed to {}",
                    object_info.ToString(),
                    output_path.string());
    }
    else {  // dump to stdout
        if (not api->RetrieveToFds(
                {object_info}, {dup(fileno(stdout))}, clargs.raw_tree)) {
            Logger::Log(LogLevel::Error, "failed to dump artifact.");
            return false;
        }
    }

    return true;
}
#endif
