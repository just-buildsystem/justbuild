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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_EXIT_CODES_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_EXIT_CODES_HPP

enum JustMRExitCodes {
    kExitSuccess = 0,
    kExitExecError = 64,             // error in execvp
    kExitGenericFailure = 65,        // none of the known errors
    kExitUnknownCommand = 66,        // unknown subcommand error
    kExitClargsError = 67,           // error in parsing clargs
    kExitConfigError = 68,           // error in parsing config
    kExitFetchError = 69,            // error in just-mr fetch
    kExitUpdateError = 70,           // error in just-mr update
    kExitSetupError = 71,            // error in just-mr setup(-env)
    kExitBuiltinCommandFailure = 72  // error executing a built-in command
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_EXIT_CODES_HPP
