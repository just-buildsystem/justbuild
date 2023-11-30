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

#ifndef INCLUDED_SRC_UTILS_CPP_GSL_HPP
#define INCLUDED_SRC_UTILS_CPP_GSL_HPP

#include "gsl/gsl"

// implement EnsuresAudit/ExpectsAudit (from gsl-lite) only run in debug mode
#ifdef NDEBUG
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ExpectsAudit(x) (void)0
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define EnsuresAudit(x) (void)0
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ExpectsAudit(x) Expects(x)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define EnsuresAudit(x) Ensures(x)
#endif

#endif  // INCLUDED_SRC_UTILS_CPP_GSL_HPP_
