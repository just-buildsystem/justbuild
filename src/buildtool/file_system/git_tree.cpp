#include "src/buildtool/file_system/git_tree.hpp"

#include <sstream>

#include "src/buildtool/logging/logger.hpp"
#include "src/utils/cpp/hex_string.hpp"

extern "C" {
#include <git2.h>
}

namespace {

constexpr auto kOIDRawSize{GIT_OID_RAWSZ};

auto const kLoadTreeError =
    std::make_shared<std::optional<GitTree>>(std::nullopt);

[[nodiscard]] auto PermToType(std::string const& perm_str) noexcept
    -> std::optional<ObjectType> {
    constexpr auto kPermBase = 8;
    constexpr auto kTreePerm = 040000;
    constexpr auto kFilePerm = 0100644;
    constexpr auto kExecPerm = 0100755;
    constexpr auto kLinkPerm = 0120000;

    int perm = std::stoi(perm_str, nullptr, kPermBase);

    switch (perm) {
        case kTreePerm:
            return ObjectType::Tree;
        case kFilePerm:
            return ObjectType::File;
        case kExecPerm:
            return ObjectType::Executable;
        case kLinkPerm:
            Logger::Log(LogLevel::Error, "symlinks are not yet supported");
            return std::nullopt;
        default:
            Logger::Log(LogLevel::Error, "unsupported permission {}", perm_str);
            return std::nullopt;
    }
}

auto ParseRawTreeObject(GitCASPtr const& cas,
                        std::string const& raw_tree) noexcept
    -> std::optional<GitTree::entries_t> {
    std::string perm{};
    std::string path{};
    std::string hash(kOIDRawSize, '\0');
    std::istringstream iss{raw_tree};
    GitTree::entries_t entries{};
    // raw tree format is: "<perm> <path>\0<hash>[next entries...]"
    while (std::getline(iss, perm, ' ') and   // <perm>
           std::getline(iss, path, '\0') and  // <path>
           iss.read(hash.data(),              // <hash>
                    static_cast<std::streamsize>(hash.size()))) {
        auto type = PermToType(perm);
        if (not type) {
            return std::nullopt;
        }
        try {
            entries.emplace(path,
                            std::make_shared<GitTreeEntry>(cas, hash, *type));
        } catch (std::exception const& ex) {
            Logger::Log(LogLevel::Error,
                        "parsing git raw tree object failed with:\n{}",
                        ex.what());
            return std::nullopt;
        }
    }
    return entries;
}

// resolve '.' and '..' in path.
[[nodiscard]] auto ResolveRelativePath(
    std::filesystem::path const& path) noexcept -> std::filesystem::path {
    auto normalized = path.lexically_normal();
    return (normalized / "").parent_path();  // strip trailing slash
}

// NOLINTNEXTLINE(misc-no-recursion)
[[nodiscard]] auto LookupEntryPyPath(
    GitTree const& tree,
    std::filesystem::path::const_iterator it,
    std::filesystem::path::const_iterator const& end) noexcept
    -> GitTreeEntryPtr {
    auto segment = *it;
    auto entry = tree.LookupEntryByName(segment);
    if (not entry) {
        return nullptr;
    }
    if (++it != end) {
        if (not entry->IsTree()) {
            return nullptr;
        }
        return LookupEntryPyPath(*entry->Tree(), it, end);
    }
    return entry;
}

}  // namespace

auto GitTree::Read(std::filesystem::path const& repo_path,
                   std::string const& tree_id) noexcept
    -> std::optional<GitTree> {
    auto cas = GitCAS::Open(repo_path);
    if (not cas) {
        return std::nullopt;
    }
    return Read(cas, tree_id);
}

auto GitTree::Read(gsl::not_null<GitCASPtr> const& cas,
                   std::string const& tree_id) noexcept
    -> std::optional<GitTree> {
    auto raw_id = FromHexString(tree_id);
    if (not raw_id) {
        return std::nullopt;
    }
    auto obj = cas->ReadObject(*raw_id);
    if (not obj) {
        return std::nullopt;
    }
    auto entries = ParseRawTreeObject(cas, *obj);
    if (not entries) {
        return std::nullopt;
    }
    return GitTree{cas, std::move(*entries), std::move(*raw_id)};
}

auto GitTree::LookupEntryByName(std::string const& name) const noexcept
    -> GitTreeEntryPtr {
    auto entry_it = entries_.find(name);
    if (entry_it == entries_.end()) {
        Logger::Log(
            LogLevel::Error, "git tree does not contain entry {}", name);
        return nullptr;
    }
    return entry_it->second;
}

auto GitTree::LookupEntryByPath(
    std::filesystem::path const& path) const noexcept -> GitTreeEntryPtr {
    auto resolved = ResolveRelativePath(path);
    return LookupEntryPyPath(*this, resolved.begin(), resolved.end());
}

auto GitTreeEntry::Blob() const noexcept -> std::optional<std::string> {
    if (not IsBlob()) {
        return std::nullopt;
    }
    return cas_->ReadObject(raw_id_);
}

auto GitTreeEntry::Tree() const& noexcept -> std::optional<GitTree> const& {
    auto ptr = tree_cached_.load();
    if (not ptr) {
        if (not tree_loading_.exchange(true)) {
            ptr = kLoadTreeError;
            std::optional<std::string> obj{};
            if (IsTree() and (obj = cas_->ReadObject(raw_id_))) {
                if (auto entries = ParseRawTreeObject(cas_, *obj)) {
                    ptr = std::make_shared<std::optional<GitTree>>(
                        GitTree{cas_, std::move(*entries), raw_id_});
                }
            }
            tree_cached_.store(ptr);
            tree_cached_.notify_all();
        }
        else {
            tree_cached_.wait(nullptr);
            ptr = tree_cached_.load();
        }
    }
    return *ptr;
}

auto GitTreeEntry::Size() const noexcept -> std::optional<std::size_t> {
    if (auto header = cas_->ReadHeader(raw_id_)) {
        return header->first;
    }
    return std::nullopt;
}
