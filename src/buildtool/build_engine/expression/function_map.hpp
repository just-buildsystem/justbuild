// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_FUNCTION_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_FUNCTION_MAP_HPP

#include <functional>
#include <string>

#include "src/buildtool/build_engine/expression/linked_map.hpp"

class ExpressionPtr;
class Configuration;

using SubExprEvaluator =
    std::function<ExpressionPtr(ExpressionPtr const&, Configuration const&)>;

using FunctionMap =
    LinkedMap<std::string,
              std::function<ExpressionPtr(SubExprEvaluator&&,
                                          ExpressionPtr const&,
                                          Configuration const&)>>;

using FunctionMapPtr = FunctionMap::Ptr;

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_FUNCTION_MAP_HPP
