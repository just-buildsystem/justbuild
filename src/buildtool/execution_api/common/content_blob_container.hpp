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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>  //std::move

#include "gsl/gsl"
#include "src/utils/cpp/hash_combine.hpp"

template <typename TDigest>
struct ContentBlob final {
    ContentBlob(TDigest mydigest, std::string mydata, bool is_exec) noexcept
        : digest{std::move(mydigest)},
          data{std::make_shared<std::string>(std::move(mydata))},
          is_exec{is_exec} {}

    ContentBlob(TDigest mydigest,
                gsl::not_null<std::shared_ptr<std::string>> const& mydata,
                bool is_exec) noexcept
        : digest{std::move(mydigest)}, data(mydata), is_exec{is_exec} {}

    [[nodiscard]] auto operator==(ContentBlob const& other) const noexcept
        -> bool {
        return std::equal_to<TDigest>{}(digest, other.digest) and
               is_exec == other.is_exec;
    }

    TDigest digest;
    std::shared_ptr<std::string> data;
    bool is_exec = false;
};

namespace std {
template <typename TDigest>
struct hash<ContentBlob<TDigest>> {
    [[nodiscard]] auto operator()(
        ContentBlob<TDigest> const& blob) const noexcept -> std::size_t {
        std::size_t seed{};
        hash_combine(&seed, blob.digest);
        hash_combine(&seed, blob.is_exec);
        return seed;
    }
};
}  // namespace std

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_CONTENT_BLOB_CONTAINER_HPP
