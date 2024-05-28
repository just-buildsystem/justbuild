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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP

#include <string>
#include <utility>  // std::move

#include "src/buildtool/common/bazel_types.hpp"

struct BazelBlob {
    BazelBlob(bazel_re::Digest mydigest, std::string mydata, bool is_exec)
        : digest{std::move(mydigest)},
          data{std::move(mydata)},
          is_exec{is_exec} {}

    bazel_re::Digest digest{};
    std::string data{};
    bool is_exec{};  // optional: hint to put the blob in executable CAS
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_BAZEL_MSG_BAZEL_BLOB_HPP
