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

#ifndef INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP
#define INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP

#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/content_blob_container.hpp"

using ArtifactBlob = ContentBlob<ArtifactDigest>;

#endif  // INCLUDED_SRC_BUILDTOOL_EXECUTION_API_COMMON_ARTIFACT_BLOB_HPP
