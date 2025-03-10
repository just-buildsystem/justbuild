// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_ATOMIC_HPP
#define INCLUDED_SRC_BUILDTOOL_FILE_SYSTEM_ATOMIC_HPP

#include <string>

class FileSystemAtomic {

  public:
    /// \brief Write file atomically
    /// First write the contents to a temporary file in the same directory; only
    /// once completed, rename the fie to the final destination.
    static auto WriteFile(std::string const& filename,
                          std::string const& content) noexcept -> bool;
};

#endif
