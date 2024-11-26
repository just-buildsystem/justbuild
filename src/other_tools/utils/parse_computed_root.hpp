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

#ifndef INCLUDED_SRC_OTHER_TOOLS_UTILS_PARSE_COMPUTED_ROOT_HPP
#define INCLUDED_SRC_OTHER_TOOLS_UTILS_PARSE_COMPUTED_ROOT_HPP

#include <optional>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/utils/cpp/expected.hpp"

class ComputedRootParser final {
  public:
    /// \brief Create a parser for computed roots
    /// \param repository Repository to be parsed
    /// \return Initialized parser on success, or std::nullopt if repository
    /// isn't Map or type is not computed.
    [[nodiscard]] static auto Create(
        gsl::not_null<ExpressionPtr const*> const& repository) noexcept
        -> std::optional<ComputedRootParser>;

    [[nodiscard]] auto GetTargetRepository() const
        -> expected<std::string, std::string>;

    [[nodiscard]] auto GetResult() const
        -> expected<FileRoot::ComputedRoot, std::string>;

  private:
    ExpressionPtr const& repository_;
    explicit ComputedRootParser(
        gsl::not_null<ExpressionPtr const*> const& repository)
        : repository_{*repository} {}
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_UTILS_PARSE_COMPUTED_ROOT_HPP
