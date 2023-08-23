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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_CLI
#define INCLUDED_SRC_BUILDTOOL_MAIN_CLI

#include "src/buildtool/common/cli.hpp"

enum class SubCommand {
    kUnknown,
    kVersion,
    kDescribe,
    kAnalyse,
    kBuild,
    kInstall,
    kRebuild,
    kInstallCas,
    kTraverse,
    kGc,
    kExecute
};

struct CommandLineArguments {
    SubCommand cmd{SubCommand::kUnknown};
    CommonArguments common;
    LogArguments log;
    AnalysisArguments analysis;
    DescribeArguments describe;
    DiagnosticArguments diagnose;
    EndpointArguments endpoint;
    BuildArguments build;
    StageArguments stage;
    RebuildArguments rebuild;
    FetchArguments fetch;
    GraphArguments graph;
    CommonAuthArguments auth;
    ClientAuthArguments cauth;
    ServerAuthArguments sauth;
    ExecutionServiceArguments es;
};

auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments;

#endif
