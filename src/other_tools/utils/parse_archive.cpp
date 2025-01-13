// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/other_tools/utils/parse_archive.hpp"

#include <utility>  // std::move

#include "fmt/core.h"
#include "src/buildtool/crypto/hash_info.hpp"
#include "src/utils/cpp/path.hpp"

auto ParseArchiveContent(ExpressionPtr const& repo_desc,
                         std::string const& origin)
    -> expected<ArchiveContent, std::string> {
    // enforce mandatory fields
    auto repo_desc_content = repo_desc->At("content");
    if (not repo_desc_content) {
        return unexpected<std::string>{
            "Mandatory field \"content\" is missing"};
    }
    if (not repo_desc_content->get()->IsString()) {
        return unexpected{
            fmt::format("Unsupported value {} for mandatory field \"content\"",
                        repo_desc_content->get()->ToString())};
    }

    auto const repo_desc_hash_info =
        HashInfo::Create(HashFunction::Type::GitSHA1,
                         repo_desc_content->get()->String(),
                         /*is_tree=*/false);
    if (not repo_desc_hash_info) {
        return unexpected{fmt::format(
            "Unsupported value {} for mandatory field \"content\"\n{}",
            repo_desc_content->get()->ToString(),
            repo_desc_hash_info.error())};
    }

    auto repo_desc_fetch = repo_desc->At("fetch");
    if (not repo_desc_fetch) {
        return unexpected<std::string>{"Mandatory field \"fetch\" is missing"};
    }
    if (not repo_desc_fetch->get()->IsString()) {
        return unexpected{
            fmt::format("Unsupported value {} for mandatory field \"fetch\"",
                        repo_desc_fetch->get()->ToString())};
    }
    auto repo_desc_distfile = repo_desc->Get("distfile", Expression::none_t{});
    auto repo_desc_sha256 = repo_desc->Get("sha256", Expression::none_t{});
    auto repo_desc_sha512 = repo_desc->Get("sha512", Expression::none_t{});
    // check optional mirrors
    auto repo_desc_mirrors = repo_desc->Get("mirrors", Expression::list_t{});
    std::vector<std::string> mirrors{};
    if (repo_desc_mirrors->IsList()) {
        mirrors.reserve(repo_desc_mirrors->List().size());
        for (auto const& elem : repo_desc_mirrors->List()) {
            if (not elem->IsString()) {
                return unexpected{fmt::format(
                    "Unsupported list entry {} in optional field \"mirrors\"",
                    elem->ToString())};
            }
            mirrors.emplace_back(elem->String());
        }
    }
    else {
        return unexpected{
            fmt::format("Optional field \"mirrors\" should be a list of "
                        "strings, but found: {}",
                        repo_desc_mirrors->ToString())};
    }

    return ArchiveContent{
        .content_hash = *repo_desc_hash_info,
        .distfile = repo_desc_distfile->IsString()
                        ? std::make_optional(repo_desc_distfile->String())
                        : std::nullopt,
        .fetch_url = repo_desc_fetch->get()->String(),
        .mirrors = std::move(mirrors),
        .sha256 = repo_desc_sha256->IsString()
                      ? std::make_optional(repo_desc_sha256->String())
                      : std::nullopt,
        .sha512 = repo_desc_sha512->IsString()
                      ? std::make_optional(repo_desc_sha512->String())
                      : std::nullopt,
        .origin = origin};
}

auto IsValidFileName(const std::string& s) -> bool {
    if (s.find_first_of("/\0") != std::string::npos) {
        return false;
    }
    if (s.empty() or s == "." or s == "..") {
        return false;
    }
    return true;
}

auto ParseArchiveDescription(ExpressionPtr const& repo_desc,
                             std::string const& repo_type,
                             std::string const& origin,
                             const AsyncMapConsumerLoggerPtr& logger)
    -> std::optional<ArchiveRepoInfo> {
    auto const archive_content = ParseArchiveContent(repo_desc, origin);
    if (not archive_content) {
        (*logger)(fmt::format("ArchiveCheckout: {}", archive_content.error()),
                  /*fatal=*/true);
        return std::nullopt;
    }
    // additional mandatory fields
    auto repo_desc_subdir = repo_desc->Get("subdir", Expression::none_t{});
    auto subdir = std::filesystem::path(repo_desc_subdir->IsString()
                                            ? repo_desc_subdir->String()
                                            : "")
                      .lexically_normal();
    if (not PathIsNonUpwards(subdir)) {
        (*logger)(fmt::format("ArchiveCheckout: Expected field \"subdir\" to "
                              "be a non-upwards path, but found {}",
                              subdir.string()),
                  /*fatal=*/true);
        return std::nullopt;
    }

    // check "special" pragma
    auto repo_desc_pragma = repo_desc->At("pragma");
    bool const& pragma_is_map =
        repo_desc_pragma and repo_desc_pragma->get()->IsMap();
    auto pragma_special =
        pragma_is_map ? repo_desc_pragma->get()->At("special") : std::nullopt;
    auto pragma_special_value =
        pragma_special and pragma_special->get()->IsString() and
                kPragmaSpecialMap.contains(pragma_special->get()->String())
            ? std::make_optional(
                  kPragmaSpecialMap.at(pragma_special->get()->String()))
            : std::nullopt;
    // check "absent" pragma
    auto pragma_absent =
        pragma_is_map ? repo_desc_pragma->get()->At("absent") : std::nullopt;
    auto pragma_absent_value = pragma_absent and
                               pragma_absent->get()->IsBool() and
                               pragma_absent->get()->Bool();
    return ArchiveRepoInfo{.archive = *archive_content,
                           .repo_type = repo_type,
                           .subdir = subdir.empty() ? "." : subdir.string(),
                           .pragma_special = pragma_special_value,
                           .absent = pragma_absent_value};
}

auto ParseForeignFileDescription(ExpressionPtr const& repo_desc,
                                 std::string const& origin,
                                 const AsyncMapConsumerLoggerPtr& logger)
    -> std::optional<ForeignFileInfo> {
    auto const archive_content = ParseArchiveContent(repo_desc, origin);
    if (not archive_content) {
        (*logger)(archive_content.error(), /*fatal=*/true);
        return std::nullopt;
    }
    auto name = repo_desc->At("name");
    if (not name) {
        (*logger)(
            "Mandatory field \"name\" for foreign file repository is missing",
            true);
        return std::nullopt;
    }
    if (not name->get()->IsString()) {
        (*logger)(fmt::format("Field \"name\" has to be a file name, given as "
                              "string, but found {}",
                              name->get()->ToString()),
                  true);
        return std::nullopt;
    }
    if (not IsValidFileName(name->get()->String())) {
        (*logger)(fmt::format("Field \"name\" has to be valid a file name, but "
                              "found {}",
                              name->get()->ToString()),
                  true);
        return std::nullopt;
    }
    auto executable = repo_desc->Get("executable", Expression::kFalse);
    if (not executable->IsBool()) {
        (*logger)(fmt::format(
                      "Field \"executable\" has to be a boolean, but found {}",
                      executable->ToString()),
                  true);
        return std::nullopt;
    }
    bool absent{};
    auto pragma = repo_desc->Get("pragma", Expression::kEmptyMap);
    if (pragma->IsMap()) {
        auto pragma_absent = pragma->Get("absent", Expression::kFalse);
        if (pragma_absent->IsBool()) {
            absent = pragma_absent->Bool();
        }
    }

    return ForeignFileInfo{.archive = *archive_content,
                           .name = name->get()->String(),
                           .executable = executable->Bool(),
                           .absent = absent};
}
