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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONTEXT_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONTEXT_HPP

#include "gsl/gsl"
#include "src/buildtool/execution_api/local/config.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/storage.hpp"

/// \brief Aggregate of storage entities.
/// \note No field is be stored as const ref to avoid binding to temporaries.
struct LocalContext final {
    gsl::not_null<LocalExecutionConfig const*> const exec_config;
    gsl::not_null<StorageConfig const*> const storage_config;
    gsl::not_null<Storage const*> const storage;
};

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_LOCAL_CONTEXT_HPP
