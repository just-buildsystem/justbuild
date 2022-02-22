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
