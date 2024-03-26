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

#include "src/buildtool/build_engine/expression/expression_ptr.hpp"

#include <string>
#include <utility>  // std::move

#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"

ExpressionPtr::ExpressionPtr() noexcept : ptr_{Expression::kNone.ptr_} {}

auto ExpressionPtr::operator[](
    std::string const& key) const& -> ExpressionPtr const& {
    return (*ptr_)[key];
}

auto ExpressionPtr::operator[](std::string const& key) && -> ExpressionPtr {
    return (*ptr_)[key];
}

auto ExpressionPtr::operator[](
    ExpressionPtr const& key) const& -> ExpressionPtr const& {
    return (*ptr_)[key];
}

auto ExpressionPtr::operator[](ExpressionPtr const& key) && -> ExpressionPtr {
    return (*ptr_)[key];
}

auto ExpressionPtr::operator[](std::size_t pos) const& -> ExpressionPtr const& {
    return (*ptr_)[pos];
}

auto ExpressionPtr::operator[](std::size_t pos) && -> ExpressionPtr {
    return (*ptr_)[pos];
}

auto ExpressionPtr::operator<(ExpressionPtr const& other) const -> bool {
    return ptr_->ToHash() < other.ptr_->ToHash();
}

auto ExpressionPtr::operator==(ExpressionPtr const& other) const -> bool {
    return ptr_ == other.ptr_ or (ptr_ and other.ptr_ and *ptr_ == *other.ptr_);
}

auto ExpressionPtr::Evaluate(
    Configuration const& env,
    FunctionMapPtr const& functions,
    std::function<void(std::string const&)> const& logger,
    std::function<void(void)> const& note_user_context) const noexcept
    -> ExpressionPtr {
    return Evaluator::EvaluateExpression(
        *this, env, functions, logger, note_user_context);
}

auto ExpressionPtr::IsCacheable() const noexcept -> bool {
    return ptr_ and ptr_->IsCacheable();
}

auto ExpressionPtr::ToIdentifier() const noexcept -> std::string {
    return ptr_ ? ptr_->ToIdentifier() : std::string{};
}

auto ExpressionPtr::ToJson() const noexcept -> nlohmann::json {
    return ptr_ ? ptr_->ToJson() : nlohmann::json::object();
}

auto ExpressionPtr::IsNotNull() const noexcept -> bool {
    // ExpressionPtr is nullptr in error case and none_t default empty case.
    return static_cast<bool>(ptr_) and not(ptr_->IsNone());
}

auto ExpressionPtr::Map() const& -> ExpressionPtr::linked_map_t const& {
    return ptr_->Map();
}

auto ExpressionPtr::Make(linked_map_t&& map) -> ExpressionPtr {
    return ExpressionPtr{std::move(map)};
}

auto std::hash<ExpressionPtr>::operator()(ExpressionPtr const& p) const noexcept
    -> std::size_t {
    return std::hash<Expression>{}(*p);
}
