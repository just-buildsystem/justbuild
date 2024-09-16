// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_DIAGNOSE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_DIAGNOSE_HPP

#include "src/buildtool/build_engine/target_map/result_map.hpp"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/main/analyse.hpp"

void DiagnoseResults(AnalysisResult const& result,
                     BuildMaps::Target::ResultTargetMap const& result_map,
                     DiagnosticArguments const& clargs);
#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_DIAGNOSE_HPP
